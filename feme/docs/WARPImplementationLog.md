# FeMe WARP implementation log

This log preserves the detailed milestone breakdown, sub-milestone states,
measurement evidence, and historical notes that support the concise
[WARPRoadmap.md](WARPRoadmap.md). Keep new detailed status and evidence here;
update the roadmap summary when milestone status or blockers change.

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
narrow callback audit. The compiler-backed continuation below now closes
`W0(g)` with a real device and application WARP-kernel resource path.
The negative public general-provider search
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
(classify the local Direct3D test corpus) is complete: its classifications,
redistribution review, SM 5.1 input obligation, and focused public-contract
tests are recorded in
[W0DirectXTestCorpusClassification.md](W0DirectXTestCorpusClassification.md).
Broader legacy/all-stage compatibility remains W2-W4 work. The earlier fixed
raw-UAV and grouped SM6 reference oracles were prerequisites, not owned-device
proof. Owned headless execution now closes W1 as recorded below.

This is the implementation checklist for [FeMeWARPDesign.md](FeMeWARPDesign.md),
not a claim that the diagnostic device is a complete Direct3D adapter. The product remains a
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

## W0 closure (2026-11-27)

W0(a), W0(d), and W0(g) are now complete. The `warp-compiler` variant uses
stock D3D12/DXGI and the unmodified WARP kernel services to create a real
application-facing device, not a replacement COM runtime. Its two initialization
shaders compile through DXSA import, DXIL raising, and the shared CPU
`CompiledStage` JIT. Compute pipelines retain that native stage; vertex-only
pipelines own a shared `graphics::GraphicsPipeline`. Immutable blend/depth/raster
state and resettable command-pool objects have validated ownership and teardown.

The strict OS gate verifies WARP vendor/device/software/LUID independently of
the owned UMD/compiler paths and compiler source revision. It requires successful
application `CreateCommittedResource`, two map/unmap rounds with retained bytes
and VA, eviction, and residency through a real runtime paging queue. The audit
counts one application HRTRESOURCE-backed KMD heap created/destroyed, two real
residency callbacks (initial allocation plus explicit residency), one eviction,
two native shader and shared pipeline lifetimes, and no objects left at teardown.
All sixteen observed caps succeed; all five table publications are checked.

The independent system-WARP buffer control and the earlier strict shader-rejection
variant remain separate tests. Callback tests pass 64/64 and OS-routing tests
37/37. The full assertions-enabled `check-feme` passes 4397 tests, with
15 skipped and 10 unsupported; its Vulkan identity check selects FeMe.
Generated compute/vertex fixtures cover native compiler/DDI ownership,
malformed bytecode, stage mismatch, immutable descriptor copies, cross-device
rejection, and pipeline retention after shader-handle destruction.
See [exact evidence and reproduction](W0WindowsIntegrationProbe.md#w0-closure-compiler-backed-device-and-application-buffer).

This was the W0 feasibility checkpoint, before the W1 implementation below.
Its diagnostic FL 11_0 creation and SM 5.1 enumeration are not feature-level
or shader-model compliance claims. This device evidence is native ARM64 on
the current Windows VM; historical container/x64/Agility routing evidence is
not a successful-device matrix.

## W1 closure: owned kernel-ordered headless compute

W1(a), W1(b), and W1(c) are complete for the selected single-adapter,
single-node internal headless scope. W2-W7 and complete FL 12_2 remain open.
The stock D3D12/DXGI runtime owns the public COM objects; FeMe owns the UMD
objects, shader compiler, native code, recorded commands and CPU execution.
No Microsoft implementation/shader fixture, ordinal 199 forwarding, substitute
COM runtime, system-binary replacement or new driver was introduced.

### Acceptance reconciliation

| Step | Owned implementation and passing evidence |
|---|---|
| ~~W1(a)~~ | Optional compiler-backed Windows build, direct/compute/copy queues, allocator epochs, typed lists, heaps/buffers, monitored fence placements, native PSOs and accounting. Native `Tools/warp-compiler-compute.dxasm` exercises invalid sizes/owners, reset/close/replay, retained submissions, partial queue-creation OOM, incomplete positive callback results with returned handles, worker failure and canceled readiness events. `os-routed-warp-compiler-owned-ordered-buffer-commands` exercises pending teardown. Exact exports and same-process DXC/Vulkan coexistence pass. Runtime/DDI handles remain outside portable compiler/execution code. |
| ~~W1(b)~~ | `os-routed-warp-compiler-owned-commands` and `os-routed-warp-compiler-owned-dxil-commands` execute resource-free application compute. The buffer and ordered-buffer gates execute 3-D IDs, partial waves, multi-wave groupshared exchange/barriers, repeated accumulation, copies and completed readback against a scalar oracle. `os-routed-warp-compiler-reference-buffer-commands` runs the same original grouped shader against system WARP as an independent control. Native dispatch/DXIL fixtures test concurrent PSOs, independent compilation contexts and retained code/pipeline lifetimes. |
| ~~W1(c)~~ | CMake generates `warp-compiler/warp-headless-capabilities.json`, schema/profile version 1, from `utils/windows-warp-interposition/WARPHeadlessCapabilities.json.in`. It records SDK/DDI requirements, limits, named evidence, configured DXC/coexistence coverage and exclusions. `complete_product_feature_level` is null. Every owned public probe queries actual wave bounds; native DXIL tests separately accept requested width 4 and reject width 8. Generated evidence configuration is not a passing-test certificate. |

### Capability inventory v1

The inventory is build-generated beside the application-local UMD. Its version
is also compiled into the public probe's diagnostic report. This establishes
the generated subset-profile mechanism, not automatic product certification.
Changing a declared contract requires updating its inventory and behavioral
gates together. A build without DXC or the coexistence dependencies still
generates the inventory but records the corresponding evidence as unavailable;
such a configuration cannot reproduce the complete W1 exit evidence.

| Contract | Normative boundary / implementation limit | Named evidence |
|---|---|---|
| Runtime/ABI | SDK 10.0.26100.0; `D3D12DDI_SUPPORTED_0072`, command-list table `0062`, command-queue table `0001`; WARP identity `1414:008c`, software flag and matching LUID. Application-local routing only. | All `os-routed-warp-compiler-owned-*` probes check device/module provenance and teardown. |
| Exports | Exactly 18 UMD and 13 compiler exports; all addresses belong to their owned image; no ordinal 199. Compiler C ABI version 1; two JIT debugger exports and MLIR's fallback TypeID resolver are explicitly included. | Every owned public probe checks the complete PE export set. |
| Shader inputs | Released SM 5.1/6.0 input profiles, bounded raw-DXBC or DXIL ProgramHeader import, application compute subset and initialization vertex only. Header/stage/model metadata must agree; full shader-model/all-stage conformance is not claimed. | `Tools/warp-compiler-compute.dxasm`, `warp-compiler-vertex.dxasm`, `warp-compiler-dxil.hlsl`; owned commands/DXIL commands gates. |
| Wave policy | `D3D12DDI_SHADER_CAPS_0012` / public OPTIONS1: min=max=total=4, WaveOps=false. Native requested width 4 succeeds; 8 fails without an object. Public SM 6.6 is not advertised. | Every owned public wave query; `Tools/warp-compiler-dxil.hlsl` requested-width cases. |
| Dispatch | `D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION` = 65535; group/wave/reflection validation reuses `FeMeDirect3D` and `CompiledStage`. Sequential groups in the bring-up executor, not a new stage ABI. | `Tools/warp-compiler-dispatch.dxasm`, DXIL fixture and owned buffer/ordered-buffer gates. |
| Roots/bindings | DDI root signature 1.1; raw SRV/UAV root descriptors with namespace/space/register identity and all-stage visibility; 32 descriptors = `D3D12_MAX_ROOT_COST` 64 DWORDs; root GPUVA alignment 16. No constants, CBVs, tables or samplers in this public subset. | Native compute/DDI fixture; owned buffer commands test immutable bindings and malformed/missing/out-of-range bindings. |
| Resources | Committed L0 upload/readback/default-UAV buffers, 64 KiB allocation alignment, maximum aligned allocation `0xffff0000` bytes from the current 32-bit WARP private allocation size. Kernel map/residency/eviction and logical resource bounds excluding padding. | Owned buffer and buffer-command gates; native resource/range and wrong-owner checks. |
| Commands/lifetimes | Direct/compute/copy queue objects, checked list/recorder masks, immutable command snapshots, allocator generations and independently retained native pipelines. Queue destruction drains/joins pending CPU work. | Native compute/DDI replay/epoch/retention cases; owned commands and ordered-buffer teardown. |
| Copies/barriers | Checked buffer ranges and `memmove`; buffer transition/UAV barriers order CPU memory. No split/ranged/aliasing/enhanced barriers or texture copies. | Owned buffer-command scalar/guard oracle; ordered-buffer gate; independent reference-buffer-commands control. |
| Fences/order | Runtime-owned monitored synchronization objects; generic GPU/CPU wait/signal callbacks and per-queue Ready/Done gates. Native fences and implicit multi-GPU are excluded. | Owned fences and ordered-buffer-commands; native fence placement/ownership, callback and worker-failure cases. |
| Error behavior | Invalid arguments/unsupported operations report errors; failed queue creation cleans partial handles; a worker failure latches once, skips later work and attempts Done signaling to release its ordered wait. No forced OS-wide removal test. | Native compute/DDI OOM, S_FALSE-with-handle, failed wait and canceled event cases; all positive public gates require no removal. |
| Coexistence | A live native `IDxcCompiler3`/DXIL blob and real FeMe Vulkan instance/physical device coexist with owned D3D12 execution. DXC compiles and Vulkan identity is queried before and after; no Vulkan logical-device execution is claimed. | `os-routed-warp-compiler-owned-coexistence`; FeMe Vulkan identity verified in its launching shell. |

### Execution and ownership

Each submitted immutable snapshot is queued on an owned frontend worker.
The UMD queues a kernel GPU signal Ready=N after preceding runtime waits,
then a GPU wait Done=N before returning Execute. The worker waits Ready
through the CPU monitored-fence callback/event, executes FeMe's native
dispatch/copy commands and CPU-signals Done=N. Later ordinary runtime queue
signals therefore cannot pass incomplete CPU writes. Public monitored
Signal/Wait go directly through the stock runtime/kernel callbacks; the UMD
does not pretend those operations arrive at its native-fence DDI slots.
The runtime CORE context handle is opaque and is passed to KT callbacks,
not reinterpreted as a raw kernel handle.

No WARP compute DMA packet decoder or reference shader execution is used in
the owned path. The separately selected reference mode is only the control.
The user-authorized reference checkout was inspected to establish callback
and opaque-context ordering semantics, not copied into FeMe.

Queue creation uses checked Win32 thread/event/fence allocation and explicit
partial cleanup. This frontend deliberately does not construct the portable
`feme::execution::TaskExecutor`, whose `std::thread`, `std::function` and
deque allocations do not expose the checked creation/submission failure path
needed at this no-exceptions DDI boundary. Both implement FIFO/drain semantics;
the Windows worker additionally owns the kernel Ready/Done handshake.
Prepared invocation, CPU stage ABI and JIT code still reuse `CompiledStage`.
Windows handles and callbacks do not leak into that portable implementation.

Submitted pipelines survive shader/PSO/list/allocator release or reset.
Resource lookup checks the live owner registry before dereferencing private
handles; shared registry guards hold mappings stable during CPU use. Buffers
must still remain alive through their submitted use, as required by D3D12.
The pending-teardown oracle releases recording objects and the consumer queue
while a delayed separate queue releases its future gate; the buffers remain
valid. An application that never satisfies its own wait can deadlock draining;
no undocumented timeout, global process kill or system device reset is used.

The original grouped oracle uses local `(5,3,2)`, groups `(3,2,1)`, 180 threads
and 16 guard words. Thirty groupshared entries exchange the next local thread's
global index across a barrier; three dispatches accumulate into the same UAV
without reupload. A root UAV mutation after recording proves snapshotting.
Completed readback checks every result and guard. The ordered mode additionally
proves that a future cross-queue gate prevents early writes. Converted legacy
DXIL exposed a groupshared global retained through `llvm.used`; removing only
the transformed globals from both LLVM retention lists, then dead constant
users, repaired shared CPU lowering. The portable regression preserves
unrelated retained globals.

### Commits and validation

Implementation steps: `8c8fe8369498` (owned grouped CPU dispatch),
`57b05d6bf98e` (typed queue/list frontend), `92c347353254` (root buffers and
copy/readback), `2eef5a731515` (owned DXIL and retention-list repair),
`d8f642f7700f` (monitored fence ownership), `31ac6e0896e4` (kernel-ordered
asynchronous execution), and `6a834ae5cfba` (exports, wave inventory and
DXC/Vulkan coexistence).

On the native ARM64 Windows VM (OS 10.0.26300, inbox D3D12/WARP
10.0.26100.9818, standalone SDK 10.0.26100.0):

- Full assertions-enabled, cached, PCH-disabled `check-feme`: **4400 passed,
  15 skipped, 10 unsupported**, 4425 total.
- Native compiler/DDI selectors: **4/4 passed**, also included in the full
  suite; portable retention-list regression included in that suite.
- Callback probes: **64/64 passed**.
- Final stock-runtime probes: **45/45 passed**, including owned original DXIL,
  future kernel waits, delayed pending teardown, exact exports, queried wave
  policy, independent system-WARP compute control and same-process coexistence.
- Generated inventory parsed as JSON; all nine declared OS gates are
  registered, all five native fixtures exist and both optional evidence
  groups are configured. The public wave queries match the inventory.

The main compiler/Vulkan DLLs were rebuilt by the full suite before the final
OS package/gate run. `VK_DRIVER_FILES` and `VK_ICD_FILENAMES` selected the
owned FeMe ICD and `vulkaninfo --summary` confirmed `FeMe CPU Vulkan Device`
in the test-launching shell. The missing `vswhere.exe` and duplicate
driver-sorting-layer warnings were nonfatal.

### Remaining product scope

This closes headless W1, not even a complete FL 11_0 graphics adapter.
The runtime's diagnostic creation profile and released shader-input
enumeration are harness plumbing, not distribution/conformance claims.
General roots/descriptors/CBVs, images/views/placed/reserved/shared resources,
complete barrier/query/indirect semantics and broader buffer operations remain
W2. Graphics, advanced FL 12_2 features, DXR, presentation and conformance
remain W3-W7. Native/implicit-multi-GPU fences, multi-adapter table isolation,
general removal wakeups, and broader OS/architecture/runtime matrices are
not established by this single-node native evidence.

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
| ~~W0(a)~~ | Exact OS/SDK/runtime/DDI configurations and reference controls are recorded. Historical host/container routing plus the native ARM64 compiler-backed device prove opening, negotiation and teardown through the selected WARP identity and unmodified kernel services. Installed-driver qualification is outside the selected contract. |
| ~~W0(b)~~ | ~~Product/compatibility decision.~~ User decision (2026-11-17): select a WARP-compatible application-local UMD drop-in using Microsoft's WARP identity/device IDs and relying on its existing, unmodified kernel-mode driver. Do not add a kernel driver, replace system WARP binaries, or build an application-local COM/DXGI runtime. "Drop-in" is limited to the selected application-local packaging/selection path; the existing probe proves routing but not a working resource/device contract or universal transparent substitution. On 2026-11-27, the user explicitly approved aligning with the WARP ABI to reuse the existing kernel driver. Do not copy Microsoft implementation code, use private ordinal 199, or modify system drivers/binaries. See [decision and limits](W0WindowsIntegrationProbe.md#w0b-decision-warp-compatible-application-local-umd). |
| ~~W0(c)~~ | ~~Classify the local test corpus: public black-box tests, WARP-private/white-box tests, DXBC/DXIL inputs, optional/preview features, display/HLK dependencies. Determine required legacy shader support from the selected contract. Review source/test redistribution permissions before reuse; author LLVM-owned focused tests from public contracts.~~ Completed 2026-11-27: corpus categories and redistribution scope are documented, the required SM 5.1 input obligation is established, and LLVM-owned focused translation/reference tests exercise the public contract. Full implementation coverage remains in W1-W4. See [W0DirectXTestCorpusClassification.md](W0DirectXTestCorpusClassification.md). |
| ~~W0(d)~~ | The strict owned-device runner checks WARP identity/LUID separately from UMD/compiler paths, compiler revision, native artifact lifetimes, application resources and cleanup. Independent system-WARP buffer and prior copy/fence controls pass; reference results remain oracle evidence only. |
| ~~W0(e)~~ | ~~Measure the no-install host integration paths: inbox/Agility runtime and reference-WARP versions, `CreateSoftwareAdapter`, executable-local WARP substitution, and an LLVM-owned rejection stub with DDI entry-point counters. Run an applicable public-API WARP queue/fence test.~~ Completed 2026-11-06; evidence and reproduction instructions in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md). This closes measurements only, not FeMe device creation. |
| ~~W0(f)~~ | ~~Container-first user-mode proof: build the standalone probe in the Windows container; run all four CTest cases and record OS/kit/runtime/WARP identities and exact outcomes before implementation. Verify reference WARP and the expected pre-DDI stub rejection; do not substitute host results.~~ Completed 2026-11-06; container evidence, an extended NuGet-WARP/Agility-core matrix, and reproduction instructions are in [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md#container-measurements). All four CTest cases passed with no discrepancy from the host measurements. This closes the container measurement step only -- it is not FeMe DDI negotiation; `W0(g)` below carries that forward. |
| ~~W0(g)~~ | The supported 0072 boundary and measured 0070/0071 rejection are resolved. Genuine FeMe initialization shaders/pipelines and typed device objects now enable stock-runtime device creation, application KMD buffer allocation/map/residency/eviction, provenance and complete cleanup. [Closure evidence](W0WindowsIntegrationProbe.md#w0-closure-compiler-backed-device-and-application-buffer). |
| ~~W0(h)~~ | ~~Bounded alternative-B investigation: isolate reference forwarding, named-export forwarding, ordinal-199 forwarding, bare owned DDI and owned adapter-table cases; compare WARP selection with generic software-adapter rejection; build/test inside the container and verify inbox/Agility runtime paths.~~ Completed 2026-11-06: owned open/query/close needs neither private ordinal nor forwarding at this stage. Explicit preloading retains diagnostic counters; a debugger also proves executable-local entry without preloading. [Evidence, exact counters and remaining limits](W0WindowsIntegrationProbe.md#alternative-b-application-local-warp-ddi-routing). No real FeMe device or supported DDI is claimed; the selected WARP-compatible product contract does not imply a functioning drop-in yet. |

The following paragraphs preserve the **historical bring-up sequence**, not
the current W0 state. At that earlier point W0(a) and W0(d) were partially done.
W0(e) supplied the host
version/loading measurements, W0(f) supplies the matching container
measurements, and both supply W0(d)'s reference-test evidence. W0(h) adds
OS-routed owned DDI evidence, including version/capability queries and close,
but still under Microsoft's existing adapter identity. W0(b)'s product choice
is closed; its 2026-11-27 private-ABI sign-off now allows the owned prototype
to align with WARP's existing kernel contract. W0(g)'s supported DDI/device/
thunk work remains open.
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
The 2026-11-26 native ARM64 six-version LIT rerun passes 28/28 tests and
asserts this allocation callback-table split: the legacy pair is present for
0010, while 0022 and later expose the core-layer pair. Pointer presence is
still not a valid resource allocation contract. An isolated 0010 device-only
experiment returned `S_OK` from `CreateDevice` after initializing driver
private storage, but `D3D12CreateDevice(11_0)` returned
`DXGI_ERROR_DRIVER_INTERNAL_ERROR`; `FillDDITable` was never called, and
runtime cleanup called `DestroyDevice`. The experimental path was removed:
it did not establish a working device and is not evidence that the public
contract is impossible or that private ABI is required.
Microsoft's public
[`PFND3D12DDI_CREATEDEVICE_0003` contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/nc-d3d12umddi-pfnd3d12ddi_createdevice_0003)
describes the callback as creating a graphics context; the experiment did not
implement one. See the [device-only experiment notes](W0WindowsIntegrationProbe.md#w0g-allocation-callback-table-assertions-and-device-only-experiment-vm-2026-11-26)
for why its failure cannot establish the behavior of a complete public DDI.
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
The [WARP-private adapter-info query](W0WindowsIntegrationProbe.md#w0g-warp-private-adapter-info-query-vm-2026-11-27)
now succeeds through the runtime callback on the ARM64 VM (version 2,
WDDM value 12800, driver-capability mask `0x1`). This verifies only the
private adapter-info query ABI. A separate opt-in probe also creates and
destroys a WARP context through the runtime's kernel callbacks using the
private context-buffer payload; the returned context handle is valid, its
private pointers remain intact, and the kernel returns a nonzero command
buffer size. This verifies context lifecycle compatibility, not a D3D12
device: the default bootstrap still returns `E_NOTIMPL`; the opt-in persistent
device attempt returns `S_OK` from `CreateDevice`, but the runtime rejects it
before `FillDDITable`. Runtime-owned resource allocation/residency remain
unimplemented. W0 is still blocked.
The negative public general-provider search and the successful WARP-specific
route are different findings; do not classify all further research as blocked.
Installed-path evaluation remains separate. No driver installation, signing
changes, system-DLL replacement or reboot was performed on host or container.

The persistent-context follow-up returns `S_OK` from the prototype's
`CreateDevice` after creating and retaining a WARP context in aligned
driver-private storage. The initial DDI 0010 attempt was rejected before
`FillDDITable`. The follow-up advertises WARP's DDI 0072 and also retains a
kernel paging queue. Its `DriverCaps=0x1` has `FullWddmDevice` and
`SupportsVirtualGpu` clear under WARP's published bit layout, so the probe now
matches WARP's non-SoftGPU path: the version escape returns version 2, then the
preferred-node escape returns a runtime-selected ordinal and engine affinity
1. The selected node and effective affinity are supplied to context creation.
The probe then sends `InitContextVirtual2` (escape ID 7) with the returned
kernel context handle and command-buffer mapping. It allocates and locks the
returned 4096-byte DMA buffer through the D3D12 core-layer allocation callback,
creates and destroys a monitored fence, and sends the `InitDevice` escape;
each call and its teardown returns `S_OK`. It also answers the subsequently
observed D3D12 options and shader caps with baseline/no-optional-feature
values. Shader-model count/list negotiation now also succeeds. The direct
harness observes cap types 1074, 1006, 1004, 1012, and 1012, followed by
`GetOptionalDDITables` and `FillDDITable(DEVICE_CORE, 936 bytes)`. That table
is not implemented: its explicit `E_NOTIMPL` is the device-creation result.
The `os-routed-warp-device-context` test verifies the KMD
initialization/cleanup path and this precise table boundary.
This advances private KMD and internal allocation lifetime evidence, not a
D3D12 resource or usable device. A native debugger identified and verified the
fix for invalid zero wave-lane limits in the shader-cap response, even with
`WaveOps` disabled. Two-pass enumeration of the diagnostic SM 5.1 input profile
resolves the subsequent shader-model failure; this is not a shader-execution
claim. The genuine DDI tables and application resource lifecycle remain the
bring-up boundary. The focused shader-cap/model regressions and callback
fixtures pass (59/59), as does the
OS-routing suite (31/31); these are not a working-device claim.

The subsequent committed-buffer callback step implements twelve typed heap
and resource DDIs using WARP KMD allocations, retained descriptions,
thread-safe maps, kernel residency callbacks and explicit error paths. Its three fixtures
bring the standalone suite to 62/62; OS routing remains 31/31. These entries
are not yet published through the incomplete core table. The OS-routed
context probe additionally demonstrates a real internal 64-KiB KMD buffer
allocation/map/remap/teardown, but not an application-created resource. Stock-runtime
device/resource creation, execution and runner provenance remain open;
direct callback fixtures do not close W0. See
[owned-buffer evidence](W0WindowsIntegrationProbe.md#w0g-follow-up-owned-committed-buffer-callbacks).

The opt-in `warp-prototype` continuation negotiates the required 0072 table
layouts (117 core entries, 67 command-list entries, queue and extended-feature
tables) and all fourteen observed capability queries. Typed unavailable
operations report errors; no queue or command execution is claimed. Empty
root signatures and null-stage shader objects have owned, counted lifetimes.
Stock D3D12 then requests a non-null executable shader during device
initialization. That request is deliberately rejected through `SetErrorCb`,
yielding `DXGI_ERROR_DEVICE_REMOVED`; it is not bypassed with an opaque shader
handle or reference execution. The strict OS test requires this exact failure,
table publication, successful caps, owned-module/lifecycle counters and complete
KMD/object teardown. The unchanged context-only variant still verifies the
earlier core-table refusal. The callback suite passes 64/64 and the OS suite
34/34; the latter also includes an independent reference-WARP upload-buffer
allocation/map/remap/eviction/residency test. These are diagnostic and oracle
results, not a usable FeMe device. W0 remains open: shader/pipeline initialization
is now a measured prerequisite of its device/resource/provenance exit gates.
The full `check-feme` gate passes 4,395 tests (15 skipped, 10 unsupported) after
verifying the application-local FeMe Vulkan ICD in the launching shell.
Assertions/caching remain enabled and precompiled headers disabled.

`W0(c)`'s test-corpus and compatibility-scope work is **complete**, recorded in
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
remain under the existing W1/W2/W3/W4 gates. This closes W0(c)'s inventory,
redistribution, scope, and focused-test acceptance; it does not claim complete
legacy compatibility or an owned FeMe device pass.
LLVM-owned reference tests now exist in the
standalone probe, but owned-DDI FeMe execution tests still need the prototype/runner
from `W0(g)`/`W1(a)`. The W0(b) product choice is closed, but these
implementation dependencies still block execution evidence, not a
blanket host/VM blocker. Device-execution and full-stage-coverage dependencies
remain in W0/W1 and W3/W4; they do not leave W0(c)'s corpus classification or
compatibility-scope decision open.

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

### Direct3D shared-plan implementation progress (2026-11-26)

The portable `FeMeDirect3D` library now contains checked plans for root
signatures, descriptor heaps, texture/buffer resource layouts, legacy and UAV
barriers, graphics PSO/raster/OM state, Feature Level 12_2 advanced options,
DXR 1.1 bounds, presentation/shared-fence contracts, and FL 12_2 capability
gating. These plans compile in the in-tree Windows build and are covered by
65 passing `FeMeDirect3DTests` unit cases. W1-specific progress adds checked
direct/compute/copy queue policy, requested wave-size bounds, thread-group/
dispatch overflow arithmetic, repeated dispatch, buffer-copy ranges and
monotonic fence targets, typed object close/reset/replay and pending-work
rules. W2 adds heap alignment, placed-resource bounds, residency budgets and image
copy-region validation;
W3 adds indexed/indirect draws, viewport/scissor, and IA layout validation;
W4 adds mesh/amplification dispatch; W5 adds
BLAS/TLAS build and compaction/update policies; W6 adds enhanced split/barrier
subresource ranges; W7 adds conformance release gating. These remain plan
contracts, not a working D3D12 device or queue.

This is implementation progress, not milestone closure: it does not create a
D3D12 device, heap/resource, command queue, PSO, renderer, ray dispatcher,
swap chain, or conformance result. W1 remains blocked on a real owned D3D12
UMD/device contract and W3-W7 remain blocked on their API/device and execution
frontends. The existing public-D3D matrix remains audit evidence only.

### ~~W1: Objects and first headless compute~~

Depends on the selected W0 integration proof (W0(a)/W0(g)) and WS2; reuses
existing `CompiledStage` and artifact reflection. Pending installed-driver,
display or HLK evaluation does not block
user-mode headless W1 once the selected boundary is proven.

| Step | Work and acceptance |
|---|---|
| ~~W1(a)~~ | Complete: optional build/export target, owned headless objects, checked errors/lifetimes, pending teardown and DXC/Vulkan coexistence. See [closure evidence](#w1-closure-owned-kernel-ordered-headless-compute). |
| ~~W1(b)~~ | Complete: owned resource-free and grouped DXIL/DXBC compute, raw-buffer accumulation, scalar/WARP controls, copies, fences, concurrent native PSOs and retained code. |
| ~~W1(c)~~ | Complete: generated v1 internal inventory, SDK/test mapping, actual four-lane wave queries and separate native requested-width tests. No complete lower feature level is claimed. |

The dated prerequisite entries below are historical. Their open-milestone
and `CreateDevice = E_NOTIMPL` statements describe those checkpoints, not
the current compiler-backed W0/W1 implementation.

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
unchanged. This advances W1(b)'s legacy compute implementation,
not general legacy/all-stage compatibility or owned D3D12 execution.
W0(g), W1(a) and W1(b) remain open.

### W2: Binding, resources, and synchronization

Depends on W1 and WS0-WS1. General CBVs and unbounded/non-uniform binding are
compiler prerequisites, not work that a DDI wrapper can hide.

| Step | Work and acceptance |
|---|---|
| W2(a) | Root Signature 1.0/1.1 parsing and immutable binding plans: register class/space/range, visibility, offsets, constants, root descriptors, tables and static samplers. Replace use-inferred cbuffer promotion with an explicit root-layout mapping; test multiple CBVs, overlapping register numbers of different classes, arrays, dynamic indices and non-32-bit accesses. |
| W2(b) | Descriptor creation/copies, execution-time preparation, volatility/static promises, unbounded and per-lane non-uniform ranges, UAV counters/atomics, and GPU-VA-to-bounded-allocation lookup. Keep bounds checks; never treat an arbitrary GPU VA as a trusted CPU pointer. Test updates after recording, heap rebinding, null/OOB descriptors, aliasing and allocation retention. |
| W2(c) | Committed/placed/reserved resources, residency/budgets, typeless view compatibility, planes/subresources, texture copy footprints and shared image/sampler preparation. Test D3D row/placement alignment separately from internal storage; exercise BC, sRGB, integer, depth/stencil and multisample operations individually. |
| W2(d) | Queue-ordered copies/dispatch/indirect commands, legacy transitions including promotion/decay and split/alias/UAV barriers, queries/predication, and host/queue fence events. Preserve the dependency model needed by enhanced barriers. Test cross-queue progress, decreasing fence values, replay, timeout/removal wakeup, and indirect argument overflow. |

#### Shared descriptor-backed CBV prerequisite

The shared CPU compiler now lowers finite byte-array-backed CBVs independently
of inline constants. CBV register namespace 3 preserves distinct b/t/u identities;
artifact ABI 9 admits that namespace and rejects older artifacts. Existing
callers retain single-CBV inline inference by default, while a compile option
allows descriptor-driven frontends to disable inference. Explicit inline
selection is still preserved.

Each homogeneous 128-bit row is reconstructed from independently bounded
16/32/64-bit components. Declared element extents, runtime descriptor extents,
dynamic row indices and finite descriptor-array indices are all checked.
Unsupported layout-backed CBVs and unsupported handle uses remain unchanged.
The new execution fixture compares scalar, JIT widths 4/8 and native AOT
results, including partial rows, short descriptors, doubles and output guards.

Full assertions-enabled, cached, PCH-disabled `check-feme`: **4403 passed,
15 skipped, 10 unsupported**. The launching shell verified the owned FeMe
Vulkan identity. This is a compiler prerequisite, not yet owned D3D12 root-CBV
or complete W2 acceptance.

#### Owned root-CBV binding

The stock-runtime frontend now accepts root CBVs alongside SRV/UAV roots,
with 256-byte GPUVA alignment and live logical-allocation bounds. The owned
compiler disables single-CBV inline inference. Recorded commands retain root
addresses, not constant-buffer contents; submission reads current data after
kernel readiness. Unused roots may remain unset, and valid extra root bindings
are filtered from shader materialization. Missing required bindings, duplicate
bindings and non-raw CBVs are still explicit errors.

Native execution covers one and multiple CBVs, different register spaces,
overlapping b/u register numbers, dynamic rows, short descriptors, updates
between executions and concurrent pipelines. Four stock-runtime gates cover
owned legacy SM 5.1, original DXC DXIL, independent system WARP and held
cross-queue execution/pending teardown. Two upload CBVs use 256-byte offsets,
poisoned alternate storage and post-record data writes; root changes after
dispatch must not alter the recorded bindings. An unused b9 root is exercised
both unset and bound. Every output and guard word is checked.

Validation: **4403** full-suite passes, **65/65** callback passes and **55/55**
stock-runtime passes. Generated inventory **v4** contains nineteen OS gates,
the distinct CBV alignment and existing native fixtures; complete feature
level remains null. Inline constants, tables/static samplers, complete
shader/root compatibility and the other W2 resource/synchronization work
remain open.

#### Owned inline constants and root-cost enforcement

Multiple inline parameters now use explicit b-register/space mappings into
the shared descriptor-backed CBV lowering. Each command owns its constant
bytes; submission cloning rebases all corresponding descriptor pointers into
the submission's own storage. This preserves replay and pending teardown
without borrowing recording-state or application setter memory. Single and
bulk setters validate indices, offsets, null inputs and subtraction-checked
ranges; partial writes preserve the other recorded values.

Root layouts enforce the aggregate 64-DWORD cost: each buffer root costs two,
and inline parameters cost their declared count. Up to 64 one-DWORD parameters
are accepted, while empty inline parameters, over-budget layouts and overlapping
CBV/inline register identities are rejected. The tests use named cbuffer fields,
not dynamically indexed cbuffer arrays, for the root-constant contract.

Native DDI tests exercise setter failure boundaries, a legal no-op setter,
64-parameter admission, root-cost overflow and same-b-register rejection.
Four more stock-runtime gates cover owned SM 5.1/DXIL, independent system WARP,
and held cross-queue execution with recording-object release. Two five-DWORD
blocks in different spaces use bulk/partial writes and poisoned source storage;
post-dispatch root writes must not modify the recorded command.

Validation: **4403** full-suite passes, **65/65** callback passes and **59/59**
stock-runtime passes. Inventory **v5** records twenty-three OS gates, 64 root
parameters and multiple inline constants. Full W2 remains open for descriptor
heaps/tables/static samplers, complete root compatibility, images/resource
classes and the remaining synchronization/query/indirect contract.

#### Stage-specific root visibility and compute isolation

Root signatures retain conventional vertex/hull/domain/geometry/pixel
visibility. Register/space/namespace collisions are legal only for disjoint
stage visibility; ALL and same-stage overlaps remain errors, including
descriptor-table ranges overlapping root descriptors. Root cost and finite
binding limits remain unchanged. Compute preparation ignores non-ALL-visible
parameters before descriptor resolution or inline binding materialization.

The native matrix checks all five conventional stages, disjoint admission,
same-stage/ALL rejection and table/root overlaps. Four stock-runtime controls
use identical b99/space999 inline registers in separate vertex/pixel roots,
with distinct recorded values, alongside live compute constants. Owned DXBC,
original DXIL, independent WARP and held execution preserve the compute byte
oracle, guards, root snapshots, replay and pending lifetime. Without compute
isolation the two irrelevant inline roots become duplicate borrowed bindings.

Validation: native selectors **4/4**, new controls **4/4**, full FeMe
**4413** passed / **15** skipped / **10** unsupported, callbacks **65/65**,
stock-runtime/coexistence **359/359** after shared DLL relinking, with confirmed
FeMe ICD identity. Inventory **v17** contains **323** unique required OS gates
and **11** native fixtures; bridge 4/artifact 11 remain unchanged.
This does not complete shader/root compatibility, static samplers, general
heaps, graphics execution or W2.

#### Shared Direct3D narrow integer store conversion

Reference controls exposed a conversion mismatch: FeMe truncated out-of-range
8-/16-bit typed integer stores, while system WARP saturated them. The public
[D3D11.3 functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm),
section 3.2.3.13, requires destination-range clamping for SINT/UINT conversion
to fewer or equal bits. The shared runtime now gates saturation on descriptor
flag bit 4, rather than changing the existing flag-clear Vulkan path.

Artifact ABI **11** rejects old runtimes that do not implement this semantic
flag; the descriptor remains 48 bytes. Scalar/two/four-component integer
typed-buffer entry points share one gated packer. Image stores, floats,
32-bit integers, masks, bounds and UAV permissions are unchanged.
A twelve-format unit matrix tests signed/unsigned endpoints and overflow,
flag-clear truncation, exact packed bytes, guards and ignored inactive,
out-of-range and read-only stores. Old artifact versions remain rejected.

Validation: focused runtime/version selectors **5/5** and full FeMe
**4413** passed / **15** skipped / **10** unsupported, with confirmed FeMe ICD
identity. Windows typed-UAV admission and its capability inventory are a
separate integration slice. No Vulkan advertisement or CTS result changes.

#### Owned complete additional typed-UAV buffer set

All 21 mapped typed buffer formats now support owned and borrowed UAV views:
the three scalar R32 formats, all fifteen mandatory additional formats, and
the three optional R32G32 formats. Compiler-backed shader caps now enable
`TypedUAVLoadAdditionalFormats`; callback-only builds remain unchanged.
Per-format support remains buffer-only, without texture/render-target or
complete feature-level claims. Windows bridge **4** materializes the shared
integer saturation flag for typed UAVs; artifact ABI is **11**.

Eighty-four controls cover every format through owned DXBC, original DXIL,
independent system WARP and held execution. Full-width scalar/vector shaders
load two aliased UAV views and an independently initialized SRV. Each invocation
owns its output texel, avoiding inter-invocation data races. Exact physical
output sizing and all-channel byte oracles verify repeated converted writes,
packed UINT/SINT/UNORM/FP16 values, untouched guards, live descriptor changes,
null SRV/UAV replacement and pending-resource/code lifetime.

The first run passed **76/84**; eight reference narrow integer controls exposed
the truncation/saturation discrepancy fixed by the shared prerequisite.
The corrected oracle and descriptor flag then pass **84/84** without
weakening conversion, channel, null or guard checks. Native compiler selectors
pass **4/4**, full FeMe **4413** passed / **15** skipped / **10** unsupported,
callbacks **65/65** and stock-runtime/coexistence **355/355**, after shared DLL
relinking. FeMe ICD identity was confirmed in the full/coexistence launch.
Inventory **v16** contains **319** unique required OS gates, **11** existing
native fixtures, bridge 4, artifact 11 and no complete feature-level claim.
No KMD or Vulkan advertisement changes and no new CTS result are claimed.

W2 remains open for other optional typed/image formats, general append/consume
payloads and dynamic counter parity, complete bindings/resources/residency,
barriers, queries, other indirect signatures and removal wakeups.

#### Owned narrow and packed typed SRVs

The subsequent v15 slice adds twelve narrow/packed typed SRV formats:
R8/RGBA8 UINT/SINT/UNORM and R16/RGBA16 UINT/SINT/FLOAT. The bridge and owned
views use exact one/two/four/eight-byte element geometry and existing shared
decoders. Buffer-only support does not imply typed UAV support: both owned
and borrowed UAV validation explicitly require scalar R32 formats, including
rejecting four-byte RGBA8 UAVs. Bridge 4 and artifact 10 remain unchanged.

Ninety-six new controls cover ordinary/mapped views through owned DXBC,
original DXIL, independent system WARP and held execution. Packed upload data
occupies a separate tail after the output seed; byte/word writes preserve
signed values, UNORM recovery and exact FP16 half-unit fractions. Copies,
post-record changes, non-uniform selection, null read/write behavior, guards
and pending ownership remain checked. Live-view `GetDimensions` contributes
the exact 180-element count independently of physical format/shader width.
The query is skipped on the final null-view replay; null loads still execute.

Initial controls passed 90/96; six legacy RGBA8_UNORM compiler crashes came
from a full macro array without its required null sentinel. A vector with an
explicit sentinel fixes that probe bug. Adding non-uniform dimension queries
then exposed the shared canonical matcher/scalarizer gap, fixed separately
in `4de6aa4c385c`. The first unrestricted query experiment also crashed the
reference process on its null-view replay; no vendor root cause or normative
null-query behavior was established. A phase check through the live CBV
keeps dimension comparisons on live views without weakening null-load tests.
An initially incorrect phase condition mixed global/index and X parity;
using the first live constant corrected the test before final validation.

Validation: new controls **96/96**, full **4412** passed / **15** skipped /
**10** unsupported, callbacks **65/65**, stock-runtime **271/271**.
FeMe ICD identity was confirmed before full/coexistence launches. Inventory
**v15** has **235** unique required OS gates and no complete feature-level
claim. No KMD, Vulkan advertisement or CTS-result change is claimed. W2
remains open for complete typed UAVs/other formats, images/resources/bindings
and synchronization.

#### Owned typed SRV component mappings

Bridge ABI **4** adds a checked D3D shader-component encoding for borrowed buffer
bindings; versions 1/2/3 are rejected at compilation and dispatch. The required
always-set bit, reserved bits and all four channel sources are validated. The
compiler explicitly converts D3D's encoding into the shared runtime swizzle.
Non-default mappings require typed SRVs; raw/structured views and borrowed UAVs
remain rejected rather than silently ignoring them.

Owned descriptors retain the mapping through copies; ordered execution refreshes
it from live heap metadata alongside resource/format/counter state. Default
mapping preserves prior behavior. Native controls check malformed encodings,
old bridges and rejection without descriptor writes. Runtime artifact ABI **10**
is unchanged from the preceding shared prerequisite.

Thirty-six controls cover all nine admitted R32 scalar/two/four-component SRV
formats through owned DXBC, original DXIL, independent system WARP and held
execution. The identical shaders are reused: post-record descriptor changes
select memory channels, missing alpha/defaults and constant zero/one. The final
null SRV selects forced one but must still contribute zero to a live output;
the other null UAV preserves prior output. Source/destination copies, replay,
non-uniform selection and pending ownership retain their existing audits.
The first harness build caught a shadowed shader-source variable; renaming the
mapping-source local resolved it before running the new controls.

Validation: native selectors **4/4**, new mapping controls **36/36**,
full **4410** passed / **15** skipped / **10** unsupported, callbacks **65/65**,
stock-runtime **175/175**. FeMe ICD identity was confirmed before full/coexistence
launches. Inventory **v14** records **139** unique required OS gates, bridge **4**
and no complete feature-level claim. The KMD, format advertisements and Vulkan
advertisements are unchanged; no new CTS run is claimed. Full W2 still remains
open for other formats, images/resources/bindings and synchronization.

#### Shared typed-buffer component-mapping prerequisite

`FemeDescriptor::Swizzle` now carries the existing image component-mapping
encoding for typed buffer loads. FP32/integer scalar, two-component and
four-component helpers apply it after decoding/default-component padding and
before narrowing. The existing swizzle helpers are reused rather than adding
another conversion implementation. Zero is identity; stores, raw/structured
accesses and counter operations are unchanged.

Artifact ABI **10** rejects older layouts; the buffer descriptor grows from
40 to 48 bytes on this 64-bit host. Two real-runtime JIT tests cover component
reordering, forced zero/one, integer signedness, absent-channel defaults and
all supported load widths. A nonzero heap index checks the actual descriptor
array stride. Masked, null, out-of-bounds element/descriptor and empty-heap
accesses return zero before mapping, even with a forced-one channel.
The existing artifact rejection test additionally rejects versions 8 and 9.

Validation: **4410** full-suite passes, **15** skipped, **10** unsupported;
**139/139** stock-runtime/coexistence passes; refreshed null/old-version
focused tests **3/3**. FeMe ICD identity was confirmed before full/coexistence
launches. The new tests passed on their first execution. A pre-existing
unhandled-format switch warning in feme-run was not changed.

No new Windows mapping admission or capability is claimed: Windows view and
bridge materialization remain open; inventory stays **v13**, bridge **3**,
with **103** required OS gates. Vulkan advertisements are unchanged; historical
Windows CTS executables remain unavailable, so this is not a new CTS result.

#### Owned bounded structured append/consume

Eight more public controls exercise AppendStructuredBuffer<uint> and
ConsumeStructuredBuffer<uint> through owned DXBC, original DXIL, independent
system WARP and held/pending teardown. This reuses the tested masked counter
helper and bridge ABI **3**, without a runtime/artifact ABI or KMD change.

Each replay appends ninety unique parity-selected invocation IDs into each
bounded output region, or consumes ninety distinct values from each separate
structured source region into non-uniform output arrays. Counter-selected data
accesses, exact final counts, all surrounding guards and unchanged consume
source data are checked. The final replay replaces the second descriptors with
nulls and compares its outputs byte-for-byte with the preceding replay.
Descriptors and seeds are updated after recording. Held submission controls
retain recorded work after list/allocator/pipeline/queue release.

Consume data lives after the two 4096-byte-aligned counters in the same real
allocation, with disjoint checked view ranges. The existing **9/9** heap,
**10** residency and **9 dispatches / 10 copies** provenance audits remain.
The first build rejected an uncaptured constexpr in the seed lambda; capturing
the thread count explicitly fixed it before the first test execution.

Validation: **8/8** new controls; full **4408** passed, **15** skipped,
**10** unsupported; callbacks **65/65**; stock-runtime **139/139**, including
all sixteen counter controls and the existing native compiler selectors.
FeMe CPU Vulkan Device identity was checked before full/coexistence execution.
Inventory **v13** records **103** unique required OS gates, bridge **3** and
no complete feature-level claim. No new CTS result or Vulkan advertisement
is claimed. General append/consume payload coverage, dynamically indexed
counter-expression parity, wider typed formats/UAVs, images and complete
bindings/resources/synchronization still prevent W2 closeout.

#### Owned structured-UAV counter views

Structured UAV descriptors now retain counter resource identity and a
4096-byte-aligned offset. Creation validates both the live data view and
exact four-byte UAV counter range; ordered execution refreshes both through
the existing live-owner resource registry. Descriptor copies preserve counter
metadata and null replacement clears it. This uses the existing WARP allocation
and monitored-fence machinery, without modifying the KMD.

Compiler bridge ABI **3** adds optional counter pointer/extent metadata.
Compilation and dispatch reject versions 1/2; counter pointer/extent pairs
must agree and represent aligned four-byte storage on a non-null structured
UAV. Native controls reject malformed kinds/extents/alignment, missing counter
resources and stale bridge versions without writes. Runtime artifact ABI **9**
and the shared descriptor layout are unchanged.

Eight controls cover increment/decrement through owned original DXBC, original
DXIL, independent system WARP and held/pending teardown. Two counters at offsets
0/4096 are seeded after recording, copied before dispatch, and read back with
all surrounding guards. Divergent fixed-counter selection and non-uniform output
array stores exercise per-active-lane masking. Each replay verifies ninety unique
returned indices per live counter, and exact final counts; the final replay nulls
the second UAV and preserves its previous outputs. Recording objects and the
queue are released while the final cross-queue submission is held.

The initial dynamically indexed counter expression passed owned output/count
checks but selected the first counter for both arrays in the system-WARP control.
No root cause is claimed. The final identical DXBC/DXIL fixture uses explicit
divergent fixed-counter branches, retaining two counters and lane-mask checks.
General dynamically indexed counter-expression parity remains unproven. Initial
owned provenance failures were the unchanged six-heap audit expectation; these
controls now require **9/9** balanced heaps, **10** residency callbacks and
**9 dispatches / 10 copies**, including warmup work.

Final validation: **4408** full-suite passes, **15** skipped, **10** unsupported;
**65/65** callback passes; **131/131** stock-runtime passes, including all eight
counter controls. FeMe ICD identity was confirmed before full/coexistence runs.
Inventory **v12** validates **95** unique OS gates, existing native fixtures,
bridge **3** and no complete feature-level claim. Wider formats, images/samplers,
general append/consume and dynamic-counter parity, complete roots/resources/
residency/barriers/queries and the rest of W2 still remain open.

#### Shared structured-UAV counter prerequisite

Opcode 70 now raises to `llvm.dx.resource.updatecounter`, with writable raw/
structured handle and constant +1/-1 admission. CPU resource lowering emits
an explicitly atomic masked counter call. The existing descriptor counter
pointer and HAS_COUNTER bit are reused without a runtime/artifact ABI change.
The helper requires a structured UAV, aligned non-null counter and zero
reserved offset; inactive or invalid accesses return zero without touching it.
Real sequentially consistent hardware atomics provide unsigned 32-bit wrapping.
Increment returns the old value and decrement the new value, matching
[the public HLSL contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/sm5-object-rwstructuredbuffer-decrementcounter).
Atomic classification makes even fully uniform calls execute once per active
lane rather than once per wave.

Two phase fixtures cover raising, bound normalization, lowering, rejected
delta/read-only shapes and four-lane scalarization. Runtime tests check exact
return values, wraparound, masks, missing flags, wrong kinds, null/out-of-range
descriptors, misaligned/null counters and 512 concurrent updates with unique
returned indices. A canonical-call unit checks memory effects and mask matching.
The first lowering FileCheck assumed unchanged functions followed rewritten
ones; it was corrected to match the pass's actual output ordering.

Validation: **4408** full-suite passes, **15** skipped, **10** unsupported,
and refreshed **123/123** stock-runtime passes with confirmed FeMe ICD identity.
No Windows counter-view/bridge materialization or new capability is claimed:
inventory remains **v11**, bridge **2**, and owned counter views remain open.
No CTS executable is present at either configured historical Windows location;
this is compiler/runtime/Vulkan-regression evidence, not a new CTS result.

#### Owned two/four-component R32 buffer SRVs

R32G32 and R32G32B32A32 UINT/SINT/FLOAT SRVs now retain their eight/sixteen-byte
element geometry through checked descriptor creation and the compiler bridge.
Explicit DXGI-to-FeMe mapping reuses the existing typed CPU conversion helpers;
borrowed extents must contain whole elements. Native tests independently check
SRV admission, wider UAV rejection without descriptor mutation, multiplication/
addition overflow without mutation, and rejected wider borrowed UAV bindings.
Foreign-owner tests use a supported scalar format so unsupported-format errors
cannot mask the ownership check.

Twenty-four stock-runtime controls cover six vector input formats through
owned DXBC, original DXIL, independent system WARP and held/pending teardown.
They read the second component with finite non-uniform descriptor selection,
nonzero first elements, post-record descriptor copies and null replacement;
scalar UAV outputs and guards are checked against signed/numerical-float oracles.

The initial wider-UAV attempt passed six reference controls but failed eighteen
owned controls at the public typed-UAV capability check. Microsoft's
[typed UAV load contract](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads)
requires the complete fifteen-format additional set before enabling
`TypedUAVLoadAdditionalFormats`; working R32 vector helpers alone are not enough.
The corrected scope is wider SRVs only, with scalar R32 UAV output. Wider formats
advertise BUFFER only, wider UAV creation/bindings are explicitly rejected,
and the additional-format flag remains false. An illegal scalar-to-vector
constructor in the first fixture attempt was corrected before the final gates.

Final validation: **4403** full-suite passes, **15** skipped, **10** unsupported;
**65/65** callback passes; **123/123** stock-runtime passes, including all
twenty-four wider controls. FeMe ICD identity was confirmed before full/Vulkan
coexistence execution. Inventory **v11** validates **87** unique required OS
gates, bridge ABI **2**, and no complete product feature-level claim.
Wider typed UAVs/the complete additional-format set, counters, images/samplers,
complete roots/resources/barriers/queries and the rest of W2 remain open.

#### Owned execution-time buffer predication

Command lists now retain resource/offset/operation predication state, with
disabled defaults and reset. Direct/indirect dispatch and buffer copies
snapshot that state individually; submission clones preserve it independently
of subsequent setters or list teardown. Ordered execution reads the exact
eight-byte live resource range, then suppresses commands whose value matches
the selected equal/not-equal-zero operation. Barriers remain unconditional.
Offset alignment, live owner/range, operation and recording/queue eligibility
are checked; failures use the existing recording or queue/device-error path.
Predicate values are not frozen when recording.

Eight controls cover direct and dispatch-only indirect execution through owned
SM 5.0, original SM 6.0 DXIL, independent system WARP and held/pending teardown.
Predicate uploads occur after recording and are copied by preceding commands.
Values include zero and bit 40, proving a full 64-bit comparison. Each list
changes both operation and offset, disables predication for another dispatch,
and suppresses a clobbering buffer copy while its transition barriers execute.
The oracle expects cumulative **2/4/5** dispatches over the three replays and
checks every output/guard. A final enabled setter exercises disabled reset on
the next recording. Native tests reject invalid operations, misalignment and
unregistered resources, and admit disabling with either comparison operation.

Final validation: **4403** full-suite passes, **65/65** callback passes and
**99/99** stock-runtime passes. Inventory **v10** validates sixty-three unique
OS gates and the unchanged bridge ABI 2. The first native compile warned about
an omitted aggregate predicate initializer; explicit member initialization
fixed it before the warning-as-error standalone build. Queries, full
transitions/splits/ranges, indirect root changes/draws and general removal
wakeups still remain open; this is not W2 closeout.

#### Owned scalar R32 typed buffer views

R32_UINT/R32_SINT/R32_FLOAT SRV/UAV descriptors retain the DXGI format beside
their exact live resource identity, offset and extent. The existing checked
element arithmetic, copies, owner lookup, UAV-flag validation and null handling
are reused. Dispatch now materializes `ResourceKind::Typed` and explicitly maps
DXGI values to FeMe's independently numbered runtime formats. Existing typed
CPU load/store helpers execute the shader; no raw reinterpretation shortcut or
new conversion implementation was introduced.

Bridge ABI **2** deliberately adds the format field and typed kind. Both
compilation and dispatch reject version 1; raw/structured/CBV inputs must not
carry a typed format, and typed bindings reject stride, partial elements and
unsupported formats. The format callback advertises buffer and typed-UAV
reads/writes for precisely these three formats, without adding texture,
sampling or attachment capabilities.

Twelve public controls cover all three formats through owned SM 5.1, original
SM 6.0 DXIL, independent system WARP and held/pending teardown. Each uses
non-uniform two-element typed SRV/UAV arrays alongside the two CBV arrays.
Signed data starts negative; float data is initialized numerically rather
than with integer bit patterns. The scalar oracle checks every output and
guard, post-record copies, distinct element offsets/values and selective final
null reads/ignored writes. Native tests cover nullable typed creation, exact
format support, unsupported-format non-mutation and old/malformed bridge
rejection. An initial native build exposed a missing DXGI header in the test
driver; adding that explicit include resolved it.

Final validation: **4403** full-suite passes, **65/65** callback passes and
**91/91** stock-runtime passes. Inventory **v9** validates fifty-five unique
OS gates and bridge version 2, with complete feature level still null. Other
typed formats, counters/atomic breadth, image/sampler views, full root/resource
compatibility and remaining synchronization still prevent W2 closeout.

#### Owned finite and non-uniform buffer descriptor arrays

Finite CBV/SRV/UAV table ranges now expand into checked individual register
bindings. Immutable plans preserve count, append/explicit offsets and spaces;
register endpoints, table endpoints, overlaps and the total 64 prepared-buffer
limit are validated. The compiler regroups complete reflected ranges into
contiguous descriptor spans and rejects missing elements. Valid extra roots
remain ignored after input validation. This uses the existing individual
register field and shared heap materializer; bridge ABI **1** is unchanged.
Unbounded ranges and general resource/sampler heap indexing remain open.

Native root tests cover arrays of every buffer namespace, zero/65-entry
rejection, the 64-entry boundary, register overflow and root-descriptor overlap
with a non-base array element. The same register in another namespace remains
valid. Public controls use four two-element ranges: SRVs, UAVs and CBVs at the
same b-registers in distinct spaces. Every lookup carries an explicit
`NonUniformResourceIndex`, with alternating selection within each wave.

Eight stock-runtime modes exercise owned SM 5.1, original SM 6.0 DXIL,
independent system WARP and held cross-queue/pending teardown for raw and
structured arrays. SRV elements start at different allocation offsets; CBV
elements carry different values; all descriptors are copied after recording.
The last iteration nulls one SRV and CBV element while a different UAV element
becomes null. This distinguishes per-element zero reads from ignored writes
and catches selecting the wrong element. Every one of the 180 results and
sixteen guards matches the independent scalar oracle.

Final validation: **4403** full-suite passes, **65/65** callback passes and
**79/79** stock-runtime passes. Inventory **v8** validates forty-three distinct
OS gates and the unchanged headless/bridge boundaries. W2 still needs typed,
counter and image/sampler views, unbounded/general heaps and full root
compatibility, resource classes/residency and complete synchronization.
Neither the runtime nor the existing WARP KMD was replaced or modified.

#### Owned raw/structured buffer views and mixed scalar tables

The owned frontend now creates raw and structured buffer SRV/UAV descriptors
and executes mixed scalar CBV/SRV/UAV tables. Views preserve the resource
handle, first element, element count and structure stride. Creation and
execution both check the live logical resource range; UAV views require that
exact resource's unordered-access flag, even when a larger/UAV-capable alias
shares its GPUVA. Checked multiplication/addition also rejects malformed null
view extents. Null SRVs/UAVs retain their descriptor kind but materialize empty
storage. Typed/image/counter views, custom component mappings and arrays remain
unsupported rather than being approximated.

Bring-up of the original SM 6.0 structured fixture exposed an unraised
`dx.op.bufferLoad.i32`: DXC extracts only component one for `uint2.y`.
Compiler prerequisite `2a6b8cfa3375` raises sparse legacy structured accesses
at checked constant byte offsets into individually bounded scalar loads. It
does not widen to unused components, change existing packed operations or
silently accept sparse raw/dynamic-offset/status operations. Focused raising
tests cover isolated/discontiguous integer/FP32 extracts, overflow and retained
unsupported forms.

Eight public gates compare owned SM 5.1, original DXC DXIL, independent system
WARP and held cross-queue/pending teardown for both raw and structured views.
The raw SRV starts sixteen bytes into its upload allocation; structured inputs
use eight-byte elements and the second component, while output elements are
four bytes. Tables combine appended SRV/UAV offsets with explicit CBV offsets.
Descriptors are created/copied after recording; subsequent table mutations do
not change the recorded bases. The final iteration uses null SRV/CBV views.
Every scalar result and all sixteen output guards match the CPU oracle.

Final validation: **4403** full-suite passes, **65/65** callback passes and
**71/71** stock-runtime passes. Inventory **v7** records thirty-five OS gates,
seven native fixtures, a 2048-byte maximum structured stride and the existing
64 prepared-buffer limit. No WARP implementation was copied or forwarded and
the KMD remains unchanged. W2 and complete feature-level conformance remain open.

#### Owned descriptor heaps and scalar CBV tables

The frontend owns checked CPU/shader-visible CBV/SRV/UAV heap storage, live
handle registries, CBV creation and CPU-source descriptor copies. Descriptor
copies validate all source/destination ranges before changing any destination,
including optional range-size arrays, ownership, alignment and shader-visible
source rejection. Root tables currently admit scalar CBV ranges, with explicit
and append offsets, register-space separation and aggregate binding limits.
Other descriptor families, arrays and unbounded/non-uniform ranges remain open.

Commands snapshot heap identity, table bases and immutable range mappings.
The worker reads current descriptors after kernel readiness, resolves bounded
live GPUVAs and preserves each CBV's view extent. Null CBVs materialize zero
descriptors; uninitialized/mismatched entries and stale/foreign heaps are
errors. Table mutations after recording do not change the recorded bases.

Stock D3D12 submits CPU descriptor heaps as pageable objects during creation.
Initially rejecting object type 24 removed the device after a successful CPU
heap constructor, before shader-visible heap creation reached the driver.
Live per-device registrations now validate that CPU metadata; paging filters
exclude it from KMD allocation lists while retaining all real heaps in mixed
lists. No fabricated allocation, GPU-handle workaround or reference-driver
forwarding was added. The process lesson is recorded in `.instructions.md`.

Native tests cover heap/copy ownership, stale and misaligned handles, atomic
copy rejection, scalar table admission and malformed range flags. Callback
tests additionally verify descriptor-only and mixed KMD residency. Four
stock-runtime gates cover owned SM 5.1/DXIL, independent system WARP and
future kernel waits/pending teardown. They copy descriptors after recording,
use distinct table/range offsets, explicitly evict/make metadata resident,
and verify null CBVs against every scalar output and guard word.

Validation: **4403** full-suite passes, **65/65** callback passes and **63/63**
stock-runtime passes. Inventory **v6** records twenty-seven OS gates, a
million-entry heap capacity and the current 64-binding/scalar-CBV table
boundary. Complete feature level remains null; W2 is still open.

#### Owned W2 buffer and indirect progress (2026-11-27)

W2 is **not closed**. Four tested implementation steps extend the owned
stock-runtime frontend without claiming the remaining resource/binding
contract:

- `f2533541132f`: placed buffers reuse the existing implicit heap buffer via
  `ReuseBufferGPUVA`, without another KMD allocation. Creation validates live
  ownership, 64 KiB alignment, rounded allocation extent, foreign/stale handles
  and duplicate destination handles. Resource ranges/VA include the placement
  offset; destruction of a heap with live placements is rejected. Independent
  placed resources retain the backing allocation and bytes.
- `d1c6e9d32b4b`: whole-buffer alias barriers use the actual `0022` ranged
  DDI, including its alias BEGIN/END flags and all-subresource/all-byte
  sentinels. Ordinary split transitions and finite ranged/atomic-copy barriers
  remain unsupported. Public monitored-fence tests now decrease values to
  zero, including while a future queue wait is held, before releasing that
  wait and observing the correctly ordered final signal.
- `86a0a0aedf76`: submitted dispatches preserve recorded GPUVAs but refresh
  their live alias extents at execution. Creating/removing an overlapping
  resource cannot freeze an obsolete larger extent into a valid surviving
  root binding; native alias lifetime cases cover both growth and shrinkage.
- `a8f70bba88d5`: dispatch-only command signatures validate stride, node,
  argument type and live ownership. Recorded indirect work retains its
  pipeline and root snapshot, but resolves bounded argument/count buffers
  after preceding commands and kernel waits. Counts clamp to the maximum;
  zero counts do no work. Each dispatch reads its padded argument record and
  checks dimensions before invoking the shared CPU executor.

The selected 0072 command table does not carry the obsolete
`D3D12DDI_RESOURCE_ALIASING_BARRIER` union. The initial attempt to use that
public-style shape failed compilation; inspection of the SDK and authorized
runtime conversion established the versioned ranged representation. No
reference implementation or executable fixture was copied.

Root GPUVAs cannot identify a particular overlapping resource. Address lookup
therefore selects the largest matching live logical buffer extent, excluding
the implicit backing allocation; a small alias cannot shadow a larger valid
binding. Resource-handle copy/view ranges remain individually bounded.
Execution refreshes binding bounds from the current live aliases while
preserving the recorded GPUVA. This is allocation-safe GPUVA handling, not a claim
that GPUVA-only roots recover the application's original resource identity.

`os-routed-warp-compiler-owned-placed-buffers` and the independent
`os-routed-warp-compiler-reference-placed-buffers` create a three-page upload
heap, two distinct 4097-byte placements and an overlapping alias. Alias writes
must survive resource creation/destruction and appear through the original
placement; GPUVAs must include offsets. A direct list records explicit/global
alias barriers and two bounded copies into readback, followed by a real fence
completion and every-byte scalar verification. The audit requires three
application KMD heaps with balanced teardown and exactly two owned copies.

Native `warp-heap-placement` additionally checks malformed/rounded bounds,
foreign and stale handles, double creation, per-resource versus GPUVA alias
bounds, and rejected premature heap destruction. Native compiler/DDI tests
check a global ranged-alias barrier and invalid resource handles. These are
owned execution/resource tests, not portable plan-only evidence.

Four additional indirect gates cover owned DXBC, original DXC DXIL, independent
system WARP and a held cross-queue wait/pending teardown. They reuse the
original (5,3,2)-thread groupshared shader and every-word scalar/readback oracle.
An upload written after recording is copied into a default argument buffer
before indirect execution. Tests use a nonzero argument offset, a 16-byte stride,
an optional count at a distinct offset, clamping 99 to two records, a zero
count, no-count execution, a skipped invalid second record and output guards.
The first reference run correctly rejected releasing the command signature
before execution; the corrected application retains it through completion.
Native DDI cases reject duplicate creation, foreign/stale signatures, invalid
stride/root associations, unsupported draw signatures and invalid argument
resources without dereferencing foreign handles.

Generated inventory **v3**, `internal-headless-w2-in-progress`, records placed
buffers, GPUVA alias bounds, whole-buffer alias barriers and dispatch-only
indirect execution, with fifteen registered OS evidence gates and the callback
placement fixture. Version 2 introduced the buffer/alias subset; v3 adds four
indirect gates. The compiler bridge remains ABI version 1 and
`complete_product_feature_level` remains null. The W1 v1 inventory above
describes its closure checkpoint; later inventories explicitly leave W2 open.
Descriptor tables/CBVs/samplers, images/texture aliasing, finite ranged barriers,
queries/predication, indirect root changes/draws and the rest of the W2
acceptance table remain unimplemented in the owned frontend.

Validation: native compiler/DDI 4/4, callback probes 65/65, stock-runtime probes
47/47 after the final alias-bound refresh, then 51/51 after indirect execution
and refreshed profile-v3 packaging. The full assertions-enabled
cached/PCH-disabled suite passed 4400 tests (15 skipped, 10 unsupported) after
the indirect implementation, including its four native selectors. FeMe Vulkan
identity is verified before each Vulkan-inclusive test launch. No driver
installation, system-binary replacement or shared-environment process reset
was used.

The dated prerequisite entries below are historical compiler/plan evidence;
their W0/W1 open-status statements do not describe the current owned frontend.

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

W2(a)/W3(b) integer 3D texel-load CPU execution progress (2026-11-24):
heap-backed signed/unsigned i32 `Texture3D::Load` now executes in any stage
through the existing integer volume-load helper. The shared matcher
requires three-component i32 XYZ coordinates, an i32 mip, constant signed
XYZ offsets and scalar/vector results up to the declared texel width
(one through four), with matching result/texel scalar types. Shared
wrapping coordinate adds and result narrowing preserve exact integer bits
without FP32 conversions, samplers or stage derivatives. Native DXIL's
omitted-offset normalization remains unchanged.

The integer runtime now checks all three per-mip extents and invalid mips
before fetching, matching the FP32 load helper. Negative/out-of-range
coordinates and invalid mips read zero, preventing higher-mip X/Y aliasing.
Existing SPIR-V integer users share this runtime bounds correction;
SPIR-V lowering is unchanged.

Transforms verify integer offset arithmetic, explicit mip/mask, narrowing,
reflection, image-only signature growth, handle cleanup and idempotence.
Mixed float/integer results, too-wide results, non-32-bit texels, non-heap
handles and dynamic/undefined/poison offsets stay unsupported. The older
FP32 fixture now retains i64 texel rejection instead of rejecting valid
i32 loads. Admission tests check dynamic, whole undefined/poison and every
individually undefined/poison XYZ offset component in fragment, vertex,
compute and unannotated functions for both FP32 and i32 loads.

Runtime tests verify exact signed/unsigned component bits, lower/upper
per-mip XYZ bounds, invalid mips and a false mask. Compiled normal/reference
compute tests distinguish mips 0/1/2, XYZ and components with all four result
widths, signed offsets, bounds, invalid mips, wave sizes 4/8, five lanes,
guards and repeated prepared dispatches. High-bit/negative values and
adjacent non-FP32-exact integers test exact-bit preservation. Fragment tests
add divergent mips, integer outputs, helpers, partial waves, masks, guards
and repeats. Actual SDK DXC SM6.6 `Texture3D<int4>`/`Texture3D<uint4>` bindless
shaders execute through normal/reference/AOT paths with signed offsets,
exact components, divergent coordinates, invalid mips, five lanes, guards
and repeats on one mip; runner YAML still prepares one mip. Dynamic-offset
admission rejects in all three paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 987 in 34.39 seconds; full FeMe LIT
passes 4,197 with 15 skipped, 10 unsupported and zero failures among
4,222 discovered tests in 105.68 seconds. Six new tests add six passes;
timings are not controlled speedups. Existing FP32 loads, sampling, other
dimensions, artifact ABI 8 and argument layouts are unchanged. Non-32-bit
3D loads, non-fragment implicit 3D sampling and real shader MinLod clamps
on ordinary `Sample` remain unsupported. Native import plus canonical CPU
execution is not owned D3D12 execution. Broader conformance, bound/static
descriptors and owned D3D12 execution remain open; `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) FP32 1D/array texel-load CPU execution progress (2026-11-24):
heap-backed `Texture1D::Load` and `Texture1DArray::Load` now execute in any
stage through the existing FP32 1D helpers. Shared admission/lowering
requires scalar i32 X or paired `(X,layer)` coordinates, an i32 mip,
constant signed scalar spatial offsets and scalar/vector FP32 results up
to the declared texel width (one through four). Offsets use wrapping
integer adds on X only; layers remain separate. Loads need no sampler or
derivatives. Native omitted undefined/poison spatial offsets raise to zero;
canonical dynamic/undefined/poison offsets remain rejected.

The shared FP32 1D/array runtime now checks per-mip width and invalid mips
before fetching, rather than allowing higher-mip X to alias another
subresource. Array layers are checked independently and are not mip-scaled;
invalid accesses return zero. Existing SPIR-V FP32 loads share this runtime
bounds correction; SPIR-V lowering is unchanged.

Transform tests cover coordinate/offset forwarding, separate layers,
result narrowing, reflection, image-only signature growth, handle cleanup
and idempotence. Malformed coordinates/offset shapes, integer texels,
too-wide results, non-heap handles and dynamic/undefined/poison offsets
remain unsupported. Admission tests check supported and rejected offsets
in fragment, vertex, compute and unannotated functions. Runtime tests
cover per-mip widths, layers, invalid mips and false masks.

Compiled normal/reference compute tests distinguish mips 0/1/2, layers,
X and components with all four result widths, positive/negative offsets,
bounds, invalid mips, wave sizes 4/8, five lanes, guards and repeated
prepared dispatches. Fragment tests cover divergent mips/layers,
invalid coordinates/layers/mips, helper masks, one/three quads, partial
waves, guards and repeats. Actual SDK DXC SM6.6 bindless plain/array
shaders execute through normal/reference/AOT paths with offsets, omitted
offsets, components, invalid mips, five lanes, guards and repeats on one
mip; runner YAML still prepares one mip. Dynamic-offset admission rejects
in all three paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling LIT passes 994 in 23.51 seconds; full FeMe LIT
passes 4,205 with 15 skipped, 10 unsupported and zero failures among
4,230 discovered tests in 77.10 seconds. Eight new tests add eight passes;
timings are not controlled speedups. Integer 1D/array loads, non-32-bit
3D loads, non-fragment implicit sampling and real shader MinLod clamps on
ordinary `Sample` remain unsupported. Existing 3D loads, sampling,
artifact ABI 8 and argument layouts are unchanged. Native import plus
canonical CPU execution is not owned D3D12 execution. Broader conformance,
bound/static descriptors and owned D3D12 execution remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) integer 1D/array texel-load CPU execution progress (2026-11-24):
heap-backed signed/unsigned i32 `Texture1D::Load` and
`Texture1DArray::Load` now execute in any stage through the existing
integer 1D helpers. Shared admission/lowering retains scalar i32 X or
paired `(X,layer)` coordinates, an i32 mip and constant signed scalar
spatial offsets. Result and declared texel scalar types must match, with
scalar/vector widths one through four and no result wider than the texel.
Wrapping adds offset only X; layers stay separate. Integer helpers and
shared narrowing preserve exact bits without FP32 conversions, samplers
or derivatives. Native omitted-offset raising is unchanged.

The shared integer runtime now checks per-mip width and invalid mips,
matching the FP32 helpers. Layers remain independently bounded, not
mip-scaled; invalid accesses read zero instead of aliasing another
subresource. Existing SPIR-V integer users share this runtime bounds
correction; their lowering is unchanged.

Transform tests verify integer offset arithmetic, explicit mip/mask,
narrowing, reflection, image-only signature growth, cleanup and
idempotence. Mixed scalar types, non-32-bit texels, too-wide results,
non-heap handles and dynamic/undefined/poison offsets remain rejected.
The existing admission unit now checks both scalar types in fragment,
vertex, compute and unannotated functions. Runtime tests verify exact
signed/unsigned component bits, per-mip X and layer bounds, invalid mips
and false masks.

Compiled normal/reference compute tests distinguish mips 0/1/2, layers,
X and components, all result widths, signed offsets, bounds, invalid
mips, wave sizes 4/8, five lanes, guards and repeated prepared dispatches.
High-bit/negative values and adjacent non-FP32-exact integers test exact
bits. Fragment tests add integer outputs, divergent mips/layers,
invalid accesses, helpers, partial waves, masks, guards and repeats.
Actual SDK DXC SM6.6 plain/array `int4`/`uint4` bindless shaders execute
through normal/reference/AOT paths with signed/omitted offsets, exact
components, invalid mips, five lanes, guards and repeats on one mip;
runner YAML still prepares one mip. Dynamic-offset admission rejects in
all three paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling plus runtime LIT passes 1,308 in 50.32 seconds;
full FeMe LIT passes 4,211 with 15 skipped, 10 unsupported and zero
failures among 4,236 discovered tests in 83.02 seconds. Six new tests add
six passes; timings are not controlled speedups. Existing FP32 loads,
3D loads, sampling, artifact ABI 8 and argument layouts are unchanged.
Non-32-bit 1D/array/3D loads, non-fragment implicit 1D/3D sampling and real shader
MinLod clamps on ordinary `Sample` remain unsupported. Native import plus
canonical CPU execution is not owned D3D12 execution. Broader conformance,
bound/static descriptors and owned D3D12 execution remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) FP32 2D/array texel-load CPU execution progress (2026-11-24):
the existing heap-backed `Texture2D::Load`/`Texture2DArray::Load` path now
uses shared admission/lowering with constant signed XY offsets and
scalar/vector FP32 results up to the declared texel width (one through
four). Paired `(X,Y)` or three-component `(X,Y,layer)` i32 coordinates
and an i32 mip are required. Wrapping integer adds affect only X/Y;
layers remain separate. Calls use the existing FP32 helpers with sample
zero, without samplers or derivatives, in any stage.

The old zero-offset-only fallback is removed from both lowering and the
image-access scan. Malformed coordinate types, padded three-component
array offsets, integer/non-FP32 texels, too-wide results, non-heap handles
and dynamic/undefined/poison or partially undefined offsets stay rejected.
The legacy array unit fixture now uses two spatial offset components,
not an offset for the layer. Native omitted undefined/poison XY offsets
raise to zero; partial offsets are preserved and remain unsupported by
CPU admission. This normalization applies to native DXIL only.

The shared FP32 2D/array runtime now checks both per-mip spatial extents
and invalid mips before fetching. Layers remain independently bounded,
not mip-scaled; invalid accesses read zero instead of aliasing another
row/subresource at higher mips. Existing SPIR-V FP32 users share this
runtime bounds correction; SPIR-V lowering is unchanged.

Transforms verify offset arithmetic, explicit mip/sample/mask, narrowing,
reflection, image-only signature growth, handle cleanup and idempotence.
Admission tests check valid and dynamic/undefined/poison offsets,
including each XY component, in fragment, vertex, compute and
unannotated functions. Runtime tests cover rectangular per-mip extents,
layers, invalid mips and false masks.

Compiled normal/reference compute tests distinguish rectangular mips
0/1/2, X/Y, layers and components with all four result widths, signed
offsets, bounds, invalid mips, wave sizes 4/8, five lanes, guards and
repeated prepared dispatches. Fragment tests add divergent mips/layers,
each spatial bounds axis, invalid layers/mips, helpers, partial waves,
masks, guards and repeats. Actual SDK DXC SM6.6 plain/array bindless
shaders execute through normal/reference/AOT paths with signed/omitted
XY offsets, components, invalid mips, five lanes, guards and repeats on
one mip; runner YAML still prepares one mip. Dynamic-offset admission
rejects in all three paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling plus runtime LIT passes 1,316 in 46.95 seconds;
full FeMe LIT passes 4,219 with 15 skipped, 10 unsupported and zero
failures among 4,244 discovered tests in 85.88 seconds. Eight new tests
add eight passes; timings are not controlled speedups. Integer 2D/array
and multisampled DXIL loads remain outside this milestone. Existing
1D/3D loads, sampling, artifact ABI 8 and argument layouts are unchanged.
Native import plus canonical CPU execution is not owned D3D12 execution.
Broader conformance, bound/static descriptors and owned D3D12 execution
remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) integer 2D/array texel-load CPU execution progress (2026-11-24):
heap-backed signed/unsigned i32 `Texture2D::Load` and
`Texture2DArray::Load` now execute in any stage through the existing
integer helpers. Shared admission/lowering retains paired `(X,Y)` or
three-component `(X,Y,layer)` i32 coordinates, an i32 mip and constant
signed XY offsets. Matching result/texel scalar types, scalar/vector
widths one through four and shared narrowing preserve exact bits without
FP32 conversions. Wrapping adds affect only X/Y; layers stay separate.
Calls pass sample zero without samplers or derivatives. Native
omitted-offset raising is unchanged.

The integer runtime now checks both per-mip spatial extents and invalid
mips, matching the FP32 helpers. Independent layer bounds are not
mip-scaled; invalid accesses return zero instead of aliasing another
row/subresource. Existing SPIR-V integer users share this runtime bounds
correction; SPIR-V lowering is unchanged.

Transforms verify integer offsets, explicit mip/sample/mask, narrowing,
reflection, image-only signature growth, cleanup and idempotence.
Mixed scalar types, non-32-bit texels, too-wide results, non-heap handles
and dynamic/undefined/poison or partial offsets remain rejected. The
existing admission unit now checks FP32/i32 and each undefined/poison XY
component in fragment, vertex, compute and unannotated functions.
Runtime tests verify exact signed/unsigned components, rectangular
per-mip X/Y bounds, layers, invalid mips and false masks.

Compiled normal/reference compute tests distinguish rectangular mips
0/1/2, X/Y, layers and components with all result widths, signed offsets,
bounds, invalid mips, wave sizes 4/8, five lanes, guards and repeats.
High-bit/negative values and adjacent non-FP32-exact integers test exact
bits. Fragment array tests add integer outputs, divergent mips/layers,
each lower/upper XY/layer/mip bound, helpers, partial waves, masks, guards
and repeats. Actual SDK DXC SM6.6 plain/array `int4`/`uint4` bindless
shaders execute through normal/reference/AOT paths with signed/omitted
offsets, exact components, invalid mips, five lanes, guards and repeats
on one mip; runner YAML still prepares one mip. Dynamic-offset admission
rejects in all three paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling plus runtime LIT passes 1,322 in 40.45 seconds;
full FeMe LIT passes 4,225 with 15 skipped, 10 unsupported and zero
failures among 4,250 discovered tests in 87.71 seconds. Six new tests add
six passes; timings are not controlled speedups. Existing FP32 loads,
1D/3D loads, sampling, artifact ABI 8 and argument layouts are unchanged.
Non-32-bit texels and multisampled DXIL loads remain outside this
milestone. Native import plus canonical CPU execution is not owned D3D12
execution. Broader conformance, bound/static descriptors and owned D3D12
execution remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) FP32 multisample texel-load CPU execution progress (2026-11-24):
heap-backed `Texture2DMS::Load` and `Texture2DMSArray::Load` now execute
in any stage. Native opcode 66 raising distinguishes `dx.MSTexture`
from `dx.Texture`: the wire mip slot becomes the explicit i32 sample
index of `llvm.dx.resource.load.ms`, not a mip. Plain `(X,Y)` and array
`(X,Y,layer)` coordinates remain integer; offsets contain only X/Y.
Native omitted undefined/poison spatial offsets normalize to zero;
partial offsets stay partial and fail CPU admission. Status-field or
whole-aggregate result uses remain unraised without emitting a duplicate
load. Integer native loads also raise, but integer MS CPU execution is
still unsupported.

Shared admission/lowering requires heap-backed FP32 texels and matching
scalar/vector results up to the declared width (one through four),
shape-matched i32 coordinates, an i32 sample and constant signed XY
offsets. Wrapping adds affect only X/Y; layers and samples remain separate.
Existing FP32 2D/array helpers receive mip zero and the live sample index,
without samplers or derivatives. MS sampling remains rejected.
The FP32 runtime now rejects samples outside the descriptor's sample count,
including negative indices represented as unsigned values, before they can
alias another texel/row/layer. Existing mip/spatial/layer bounds and masks
remain enforced. SPIR-V FP32 loads share this runtime correction;
SPIR-V lowering is unchanged.

Transforms verify operand forwarding, narrowing, reflection, image-only
signature growth, cleanup and idempotence. Every-stage admission checks
dynamic, whole undefined/poison and individually undefined/poison XY
offsets. Runtime tests distinguish samples/components and rectangular
X/Y/layer bounds with sample counts 1/2/4, invalid samples/mips and masks.
Compiled normal/reference compute tests distinguish all four samples,
X/Y/layers/components and result widths, signed offsets, bounds, waves
4/8, five lanes, guards and repeats. Fragment array tests cover divergent
samples/layers, each lower/upper X/Y/layer/sample bound, helper masks,
one/three quads, partial waves, guards and repeats.
Actual SDK DXC SM6.6 plain/array FP32 shaders pass native import/raising
and AOT compilation; integer shaders raise and explicitly fail CPU
admission. Native MS execution through runner YAML is not claimed:
the runner still rejects MS image preparation. Dynamic-offset admission
rejects through normal/reference/AOT paths.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling plus runtime LIT passes 1,330 in 36.64 seconds;
full FeMe LIT passes 4,233 with 15 skipped, 10 unsupported and zero
failures among 4,258 discovered tests in 78.46 seconds. Eight new tests
add eight passes; timings are not controlled speedups. Single-sample
loads, sampling, artifact ABI 8 and argument layouts are unchanged.
Integer MS CPU loads, runner MS preparation, broader conformance,
bound/static descriptors and owned D3D12 execution remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) integer multisample texel-load CPU execution progress (2026-11-24):
heap-backed signed/unsigned i32 `Texture2DMS::Load` and
`Texture2DMSArray::Load` now execute in any stage. Shared admission
retains paired `(X,Y)` or three-component `(X,Y,layer)` i32 coordinates,
an i32 sample and constant signed XY offsets. Matching result/texel
scalar types and scalar/vector widths one through four use the existing
integer helpers and shared narrowing without FP32 conversions. Wrapping
adds affect only X/Y; layers and samples remain separate. Helpers receive
mip zero and the live sample without samplers or derivatives.
Native opcode 66 raising and omitted-offset normalization are unchanged.

Integer runtime sample bounds now match FP32, preventing negative/upper
sample indices from aliasing another texel/row/layer. Existing spatial,
layer, mip and mask checks remain enforced. SPIR-V integer users share
this runtime correction; SPIR-V lowering is unchanged.
Transforms verify integer offsets, mip/sample/mask forwarding, narrowing,
reflection, image-only signature growth, cleanup and idempotence.
Mixed scalar types, non-32-bit texels, too-wide results, non-heap handles
and dynamic/undefined/poison or partial offsets remain rejected.
Every-stage admission checks FP32/i32 loads and each individually
undefined/poison XY offset component. Dynamic integer offsets fail
normal/reference/AOT admission.

Runtime tests preserve exact signed/unsigned components with sample
counts 1/2/4, rectangular spatial/layer bounds, invalid samples/mips and
false masks. Compiled normal/reference compute tests distinguish all
four samples, X/Y/layers/components and result widths, signed offsets,
bounds, waves 4/8, five lanes, guards and repeats. High-bit/negative and
adjacent non-FP32-exact integers verify bit preservation. Fragment array
tests add integer outputs, divergent samples/layers, each lower/upper
X/Y/layer/sample bound, helper masks, one/three quads, partial waves,
guards and repeats. Actual SDK DXC SM6.6 plain/array `int4`/`uint4`
shaders pass native import/raising and AOT compilation. Native MS
execution through runner YAML is not claimed: MS image preparation
remains rejected.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes.
Focused CPU/DXIL/sampling plus runtime LIT passes 1,335 in 44.00 seconds;
full FeMe LIT passes 4,238 with 15 skipped, 10 unsupported and zero
failures among 4,263 discovered tests in 85.76 seconds. Five new tests
add five passes; timings are not controlled speedups. Existing FP32 MS
loads, single-sample loads, sampling, artifact ABI 8 and argument layouts
are unchanged. Non-32-bit texels, MS sampling, runner MS preparation,
broader conformance, bound/static descriptors and owned D3D12 execution
remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.
No new adapter-matrix or external Vulkan CTS result is claimed.

W2(a)/W3(b) runner multisample preparation and native execution progress
(2026-11-24): `feme-run` heap YAML now accepts `dimension: 2d-ms` and
`2d-ms-array` with a positive `sample-count` (default 1). Non-MS
dimensions still require count 1. MS extents must have nonzero X/Y and
no depth component; MS arrays require positive physical `array-layers`.
The shared `ImageEntry` schema applies to both `images` and
`image-bindings`, without adding owned D3D12 descriptor materialization.

The runner extends its existing checked one-mip layout path:
`RowPitch = Width * ElementSize * SampleCount`, `SlicePitch =
RowPitch * Height`, with layers outside the slice and each texel's
samples contiguous. `SampleStride` is the format element size for
multiple samples, otherwise zero. Row/slice/layer overflow and host
allocation limits are diagnosed before allocation. Zero-fill, word-data
copying, single-sample defaults and descriptor lifetimes across repeated
dispatches are unchanged. Counts are CPU descriptor inputs, not a D3D12
MSAA-quality/support advertisement.

Canonical runner tests exercise counts 1/2/8, last-row/layer addressing,
zero-filled unspecified sample data, lower/upper sample bounds, waves
4/8, ten lanes, guards and repeats in normal/reference/object modes.
Validation tests reject count zero, non-MS count greater than 1, empty/
depth MS extents, zero/non-array layer misuse, row/slice/layer overflow
and host allocation limits, including the image-binding schema and
object mode. The old MS-dimension rejection fixture now retains unknown
dimension rejection.

Actual SDK DXC SM6.6 plain/array `float4`/`int4`/`uint4` MS shaders now
execute through normal/reference/AOT-object paths, not merely compile.
The four-sample fixture distinguishes samples 0/1/2/3, rows, layers and
components, with signed/omitted XY offsets, negative/upper samples,
negative/upper layers, waves 4/8, seven lanes, guards and repeats.
Integer outputs preserve high-bit/negative and non-FP32-exact values.
Native import/raising checks remain in the same fixture.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2 feme-run` build
passes, with the existing `X8_D24_UNORM` switch warning. Focused all
`feme-run` plus CPU runtime LIT passes 414, marks 4 unsupported and has
zero failures in 38.03 seconds. Two new fixtures add two passes; the
existing native fixture now also executes. This is targeted validation,
not a new full-suite, adapter-matrix or external Vulkan CTS result.
Mip-chain YAML, non-32-bit DXIL texels, MS sampling, broader conformance,
bound/static D3D12 descriptors and owned D3D12 execution remain open.
Artifact ABI 8 and argument layouts are unchanged;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) runner mip-chain preparation and native load execution progress
(2026-11-24): `images`/`image-bindings` now accept `mip-levels`, default 1,
in the CPU descriptor range [1, 32]. MS dimensions still require one mip.
Chains require nonzero extents; each spatial extent halves per level,
clamped to one. Array/cube physical layers remain fixed, while 3D depth
halves. These are CPU storage inputs, not D3D12 mip-count/format support.

The runner retains checked layout construction, following the shared
image geometry's mip-major order: each mip contains all physical
layers/depth slices, then rows, texels, samples and format components.
Every row/slice/layer multiplication and cumulative mip offset is checked
before allocation; host allocation limits are rejected. Per-image layout
vectors and backing buffers remain owned across prepared/repeated
dispatches. Single-mip defaults, including legacy zero-sized non-MS
images, zero-fill and word-data copying are unchanged. The shared image
layout helper is not used because its arithmetic is currently unchecked.

Canonical normal/reference/AOT-object tests distinguish three rectangular,
non-power-of-two mips for 1D/2D plain/array and 3D images, fixed array
layers, shrinking depth, invalid mip/depth bounds, mip 31 versus 32,
zero-filled trailing data, waves 4/8, five lanes, guards and repeats.
Cube/array gradient tests distinguish mip-major faces, the second cube
and a linear half-mip blend with divergent MinLod clamps. Image-binding
preparation is checked separately without claiming bound-image execution.
Invalid YAML tests reject zero/33 mip counts, MS mip chains, zero chain
extents, subresource/cumulative overflow and host limits in normal,
reference and object paths.

Actual SDK DXC SM6.6 FP32/SINT/UINT 2D/plain-array load fixtures now also
execute two real mips through normal/reference/AOT-object paths with
signed offsets, distinct layers/components, lower/upper mip bounds,
waves 4/8, five lanes, guards and repeats. Integer results preserve
high-bit/negative and non-FP32-exact values. Original one-mip fixture
coverage is retained. Native 1D/3D multi-mip execution is not newly claimed.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2 feme-run` build
passes with the existing `X8_D24_UNORM` switch warning. Focused all
runner plus CPU runtime LIT passes 417, marks 4 unsupported and has zero
failures in 53.06 seconds. Three new fixtures add three passes. This is
targeted validation, not a new full-suite, adapter-matrix or external
Vulkan CTS result. No lowering/runtime or artifact ABI 8/argument-layout
change is made. Non-32-bit DXIL texels, MS sampling, broader conformance,
bound/static D3D12 descriptors and owned D3D12 execution remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native 1D/array and 3D multi-mip load execution evidence
(2026-11-24): the existing SDK DXC SM6.6 FP32/SINT/UINT fixtures now also
execute three real mips through normal/reference/AOT-object paths.
This supersedes the preceding milestone's native 1D/3D evidence boundary;
no compiler, runtime, runner schema or artifact ABI change was needed.

The 1D fixture uses widths 5/2/1, retaining two independent array layers
at every mip. Signed scalar offsets select first/last texels and distinct
components. Negative/upper mips, upper X and negative/upper array layers
read zero. The 3D fixture uses rectangular extents 4x2x2, 2x1x1 and 1x1x1,
with signed XYZ offsets selecting first/last texels and components.
Per-mip upper X/Y/Z and negative/upper mips read zero rather than fetching
another row, slice or mip. Signed/unsigned results preserve high-bit/
negative and adjacent non-FP32-exact integers without floating conversion.

All six added DXC variants pass native import/raising with operand checks,
normal execution at waves 4/8, reference execution and ARM64 AOT-object
execution, each repeated twice. Five lanes exercise partial waves; guard
words remain intact. Original single-mip import/normal/reference/AOT
coverage is retained in all four fixtures.

Focused LIT passes all four changed fixtures with zero failures in
4.01 seconds. These are expanded existing tests, not new discovered
tests. No C++/runtime implementation changed, so no new build or broader
suite run was needed; no full-suite, adapter-matrix or external Vulkan
CTS result is claimed. Non-32-bit DXIL texels, MS sampling, broader
conformance, bound/static D3D12 descriptors and owned D3D12 execution
remain open. Artifact ABI 8 and argument layouts are unchanged;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) FP32 2D/array explicit-LOD sampling progress (2026-11-24):
heap-backed `Texture2D::SampleLevel` and `Texture2DArray::SampleLevel`
now use the shared checked FP32 sampling path in any stage, replacing
their old zero-offset-only fallback. Default samplers, paired `(U,V)`
or three-component `(U,V,layer)` FP32 coordinates, FP32 LOD, constant
signed XY offsets and scalar/vector results up to the declared texel
width (one through four) are supported. Layers remain separate from
spatial offsets.

The existing 2D/array helpers receive the live explicit LOD with
`UseExplicitLod=true`, four zero derivatives, zero bias and no shader
MinLod clamp. Sampler-owned LOD limits remain in the shared runtime.
Admission, lowering and signature-growth matching agree: malformed
coordinates, padded offsets, dynamic/undefined/poison or partial
offsets, non-heap handles, comparison samplers, non-FP32 texels and
too-wide results are rejected, including through the old fallback.
Native omitted XY offsets still normalize to zero during DXIL raising.
Implicit 2D and cube sampling eligibility/lowering remain unchanged.

Transforms verify signed offsets, live LOD/explicit flag, zero
derivatives/bias, separate layers, widths one through four, reflection,
cleanup and idempotence. Every-stage admission checks valid offsets
and each undefined/poison XY component. Compiled fragment array tests
vary mips/layers/coordinates with all result widths, signed offsets,
helper-only lanes, one/three quads, waves 4/8, guards, masks and repeats.
Actual SDK DXC SM6.6 2D/plain-array shaders execute through normal/
reference/AOT-object paths with three rectangular mips, signed/omitted
offsets, distinct rows/layers/components, fractional LOD blends at
0.5/1.5, negative/upper LOD, sampler min/max clamps, waves 4/8, five
lanes, guards and repeat 2. Invalid canonical XY offsets explicitly
fail normal/reference/AOT admission.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build of
affected tools and CPU units passes with the existing runner
`X8_D24_UNORM` warning. Focused CPU transforms/CPU units/runner/runtime
LIT passes 1,630 with 4 unsupported and zero failures in 77.38 seconds.
Three new fixtures and two unit tests add five passes. This is targeted
validation, not a new full-suite, adapter-matrix or external Vulkan CTS
result. Runtime/SPIR-V lowering, artifact ABI 8 and argument layouts are
unchanged. Broader sampling conformance, bound/static D3D12 descriptors
and owned D3D12 execution remain open; `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native 1D/array and 3D multi-mip explicit-LOD sampling
progress (2026-11-24): the existing SDK DXC SM6.6 FP32 `SampleLevel`
fixtures now execute three real mips, in addition to their original
single-mip regressions. 1D/plain-array widths 5/2/1 retain two fixed
layers; rectangular 3D extents 4x2x2, 2x1x1 and 1x1x1 shrink depth.
Distinct spatial/component values, constant signed scalar/XYZ offsets
and omitted offsets distinguish each level and array layer.

Five lanes select LOD -1, 0.5, 1, 1.5 and 3: negative/upper image clamps
and exact half-mip blends are checked alongside sampler MinLod=MaxLod=1.
New outputs store FP32 bits with `asuint`, not truncating numeric casts,
so the expected blend values are checked exactly. Both variants pass
native import/raising, normal waves 4/8, reference and ARM64 AOT-object
execution, with output guards and repeat 2. Original one-mip execution
remains and also gains import/raising checks.

Focused LIT passes both expanded fixtures with zero failures in
11.91 seconds. These are expanded existing tests, not new discovered
tests. No compiler/runtime implementation changed, so no new build or
broader/full-suite run was needed. This is native CPU execution evidence,
not owned D3D12 execution, D3D12 mip/format support, an adapter-matrix or
external Vulkan CTS result. Broader sampling conformance, MS sampling,
bound/static D3D12 descriptors and owned-device execution remain open.
Artifact ABI 8 and argument layouts are unchanged; `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) FP32 cube/array explicit-LOD sampling progress (2026-11-24):
heap-backed `TextureCube::SampleLevel` and `TextureCubeArray::SampleLevel`
now use the shared checked sampling path in any stage. Default samplers,
three-component directions or four-component direction/array-element
coordinates, FP32 LOD and scalar/vector results up to the declared FP32
texel width (one through four) are supported. Cubes have no spatial
offset: the canonical unused scalar i32 placeholder may be zero, undef
or poison, matching existing cube bias/gradient conventions.

The cube helpers receive live LOD with `UseExplicitLod=true`, six zero
direction derivatives, zero bias and no shader MinLod clamp. Array
elements remain separate. Admission, lowering and signature-growth
matching agree; all `SampleLevel` calls now avoid the legacy fallback,
which retains only implicit 2D/cube sampling. Malformed coordinates,
padded/nonzero/dynamic offsets, comparison samplers, non-heap handles,
non-FP32 texels and too-wide results reject consistently. Other sampling
and runtime/SPIR-V lowering are unchanged.

Transforms verify LOD/explicit flag, zero derivatives/bias, result widths,
layer forwarding, reflection, cleanup and idempotence, including negative
fallback checks. Every-stage admission checks all supported placeholders
and nonzero/dynamic rejection. Compiled fragment tests distinguish all
six faces, two array elements, three mips, spatial/component values and
widths one through four with helper-only lanes, waves 4/8, one/three
quads, output masks, guards and repeated invocations. Actual SDK DXC
SM6.6 cube/plain-array shaders execute two real mips through normal waves
4/8, reference and ARM64 AOT-object paths, with exact FP32-bit half-mip
blends, negative/upper LOD, sampler MinLod=MaxLod=1, all faces/elements,
thirteen lanes, guards and repeat 2. Invalid padded canonical offsets
explicitly fail normal/reference/AOT admission.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` affected-tool/
CPU-unit build passes. Relevant CPU transforms/units, runner and runtime
LIT passes 1,635 with 4 unsupported and zero failures in 76.36 seconds;
three new fixtures and two units add five passes. This completes bounded
shared FP32 explicit-LOD sampling for the seven ordinary raised texture
shapes, not broader sampling conformance or owned-device execution. No
new full-suite, adapter-matrix or external Vulkan CTS result is claimed.
Bound/static D3D12 descriptors, MS sampling and owned-device execution
remain open. Artifact ABI 8 and argument layouts are unchanged;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native FP32 2D/array and 3D explicit-gradient sampling
progress (2026-11-24): actual SDK DXC SM6.6 heap-backed `SampleGrad`
and shader-clamped `SampleGrad` now execute through normal/reference/
ARM64 AOT-object paths against three real mips. This extends native
execution evidence beyond the existing seven-shape pixel-shader
import/raising fixture and canonical compiled-stage tests.

Rectangular 2D/plain-array extents 4x2, 2x1 and 1x1 retain two fixed
array layers; 3D extents 4x2x2, 2x1x1 and 1x1x1 shrink depth. Live
U/dX and negative V/dY gradients select mips 0/1/2; the 3D variant
also isolates W/dX. Shader MinLod floors 0.5/1.5 produce half-mip blends
checked as exact FP32 bits, not truncating integer casts. Independent
plain-gradient samples use sampler MinLod=MaxLod=1. Signed/omitted XY/
XYZ offsets, distinct texels/components and alternating array layers
are preserved. Import/raising checks verify plain/clamped intrinsics
and offset/clamp forwarding. Both fixtures execute with normal waves
4/8, reference and ARM64 AOT-object paths, five lanes, guards and repeat 2.

Focused gradient LIT passes six tests with zero failures in 19.23 seconds.
Two new native fixtures add two discovered tests; existing import and
canonical execution regressions remain. No compiler/runtime implementation
changed, so no new build or broader/full-suite run was needed. Existing
implicit sampling, artifact ABI 8 and argument layouts are unchanged.
This is native CPU execution evidence, not owned D3D12 execution, an
adapter-matrix or external Vulkan CTS result. Native 1D/cube gradient
execution is not added here. Broader sampling conformance, MS sampling,
bound/static D3D12 descriptors and owned-device execution remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native FP32 1D/array and cube/array explicit-gradient sampling
progress (2026-11-24): actual SDK DXC SM6.6 heap-backed plain/shader-clamped
`SampleGrad` now executes three real mips through normal waves 4/8,
reference and ARM64 AOT-object paths. Together with the preceding spatial
fixtures, native gradient execution evidence spans all seven ordinary
raised texture shapes.

1D/plain-array widths 4/2/1 retain two fixed layers. Scalar U/dX and
negative U/dY distinguish mips 0/1/2, with signed/omitted scalar offsets
and distinct RGBA components. Scalar FP32 cube/plain-array texels use
square face extents 4x4, 2x2 and 1x1 with six/twelve physical faces.
Face-local tangent gradients on both screen axes distinguish mips
0/1/2 across all six faces and both array elements. Scaling directions
and gradients together by one/two preserves the projected footprint,
checking raw-major-axis handling rather than an LOD-zero approximation.

Shader MinLod floors 0.5/1.5 check exact FP32-bit half-mip blends;
independent plain-gradient samples check sampler MinLod=MaxLod=1.
Native import/raising checks scalar/shape-matched gradients/results
and clamp/offset forwarding, preserving unused scalar cube offsets.
Five/thirteen lanes, output guards and repeat 2 are covered.

Focused gradient LIT passes twelve tests with zero failures in 21.35
seconds. Two new native fixtures add two tests; retained native spatial,
import, canonical and transform regressions pass. No compiler/runtime
implementation changed, so no rebuild or broader/full-suite run was
needed. Implicit sampling, artifact ABI 8 and argument layouts are
unchanged. This is bounded native CPU execution evidence, not external
Vulkan CTS, adapter-matrix or owned D3D12 execution. Broader conformance,
MS sampling, bound/static D3D12 descriptors and owned-device execution
remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native FP32/SINT/UINT 2D/array three-mip texel-load progress
(2026-11-24): the existing SDK DXC SM6.6 heap-backed `Load` fixtures now
also execute rectangular non-power-of-two extents 5x3, 2x1 and 1x1,
retaining two fixed array layers. Signed XY offsets select first/last
texels; omitted offsets remain zero. Five lanes distinguish mips
-1/0/1/2/3, separate layers and RGBA components.

Twelve output words per lane check valid plain/array texels, negative/
upper mips and each independent lower/upper X/Y/layer bound. Invalid
accesses read zero instead of aliasing the next row/layer/subresource.
New FP32 data includes negative quarter-valued components, stored with
`asuint` for exact-bit checks rather than numeric casts. SINT/UINT data
checks high-bit/negative and adjacent non-FP32-exact integers without
float conversions. Output uses the existing scalar raw-store path;
no wider raw-store admission is added.

All three variants pass native import/raising, normal waves 4/8,
reference and ARM64 AOT-object execution, with 248-byte guarded output,
five lanes and repeat 2. Original one- and two-mip execution regressions
remain; the two-mip variants also gain import/raising checks.
Focused LIT passes both expanded fixtures with zero failures in 5.74
seconds. No new discovered tests, compiler/runtime implementation,
rebuild or broader/full-suite run is claimed.

Implicit sampling, artifact ABI 8 and argument layouts are unchanged.
This is bounded native CPU execution evidence, not D3D12 mip/format
support, external Vulkan CTS, an adapter-matrix or owned-device result.
Bound/static D3D12 descriptors, broader conformance and owned-device
execution remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) packed raw/structured DXIL store progress (2026-11-24):
opcode 140 `RawBufferStore` and legacy opcode 69 `BufferStore` now raise
contiguous low-order i32/FP32 write masks 3/7/15 into two/three/four-
component `llvm.dx.resource.store.rawbuffer` calls. Scalar mask-1 behavior
is preserved. Empty, sparse, dynamic and oversized masks, packed
non-32-bit types and mismatched active component types remain unraised,
without emitting partial vector construction or duplicate writes.
Raw unused coordinates normalize to poison; structured element indices
and intra-element byte offsets remain separate.

The existing CPU vector helpers already implement these writes. Raising
retains one packed operation, not scalar stores that could partially
commit an out-of-bounds vector. CPU lowering preserves vector width,
zero-extends raw byte addresses and computes structured stride/index/
sub-offsets without changing the runtime, SPIR-V path or ABI.

Portable raising and CPU tests check component order, both encodings,
malformed-call preservation, cleanup and repeated-pass idempotence.
Actual SDK DXC SM6.6 heap-backed and SM6.0 bound compute shaders execute
`Store2`/`Store3`/`Store4` and structured uint4/float3 field writes through
normal waves 4/8, reference and ARM64 AOT-object paths. Five lanes,
divergent masks, exact high-bit/non-FP32-exact integer and negative/
fractional FP32 values, per-width/per-record guards and two explicitly
checked repeats pass. Three-component stores write exactly 12 bytes;
partially out-of-bounds packed writes and large byte addresses leave all
words unchanged. Native metadata checks distinguish reconstructed
structs from legacy stride placeholders. Legacy AOT uses an explicit
physical heap, not newly enabled object-mode binding materialization.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` affected-tool/
CPU-unit build passes; changed-line clang-format is clean. Relevant
DXIL/CPU transforms, CPU units, runner and runtime LIT pass 1,711 with
4 unsupported and zero failures in 70.85 seconds. Three new fixtures add
three tests; retained scalar/legacy regressions pass. This is bounded
native CPU execution, not full-suite, external Vulkan CTS, adapter-matrix
or owned D3D12 evidence. Loads/status handling, image/sampling behavior,
runtime/SPIR-V implementation, artifact ABI 8 and argument layouts are
unchanged. Bound/static D3D12 descriptors, broader conformance and
owned-device execution remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) packed raw/structured DXIL load progress (2026-11-24):
opcode 139 `RawBufferLoad` now raises contiguous masks 1/3/7/15;
legacy opcode 68 `BufferLoad` infers width only from a complete leading
run of consumed result components. Packed widths two/three/four admit
i32/FP32, preserving scalar type support. Modern explicit widths remain
intact even when only component zero is consumed. Sparse/isolated-later
legacy uses, invalid masks, packed non-32-bit/mixed types, insufficient
return fields, status/aggregate uses and unused results remain unraised
without partial mutation or duplicate reads.

Existing result-replacement and CPU vector helpers preserve one packed
load, raw unused-coordinate poison, byte-address zero-extension, structured
strides/sub-offsets and full-width bounds. Portable raising/CPU regressions
check both encodings, all FP32 widths, reordered/duplicate extracts,
modern partial uses, scalar i64 preservation, malformed calls and
repeated-pass cleanup/idempotence.

Actual SDK DXC SM6.6 heap and SM6.0 bound compute shaders execute
`Load2`/`Load3`/`Load4` and structured uint4/float3 fields through normal
waves 4/8, reference and ARM64 AOT-object paths. High-bit/non-FP32-exact
integer and negative/fractional FP32 bits, five lanes, divergent uses,
input/record/output guards and two explicitly checked repeats pass.
A 784-byte output checks every word; last-valid two/three/four-component
reads succeed, while partly out-of-bounds vectors and large unsigned byte
addresses return all zero. A separate tail descriptor keeps differently
sized legacy source loads from coalescing at the same address; general
legacy partial/coalesced-width semantics are not established. Legacy AOT
uses an explicit physical heap, not object-mode binding YAML.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` affected-tool/
CPU-unit build passes 25 steps; changed-line clang-format is clean.
Relevant DXIL/CPU transforms, CPU units, runner and runtime LIT pass
1,714 with 4 unsupported and zero failures in 74.71 seconds; three new
fixtures add three tests. This is native CPU evidence, not full-suite,
external CTS, adapter-matrix or owned D3D12 execution. Stores, status
handling, images/sampling, runtime/SPIR-V implementation, artifact ABI 8
and argument layouts are unchanged. Broader conformance, bound/static
D3D12 descriptors and owned-device execution remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W3(b) native packed FP32 structured-buffer breadth (2026-11-24):
the new [`packed-structured-float.test`](../test/Tools/feme-run/HLSL/packed-structured-float.test)
executes actual SDK DXC SM6.6 heap and SM6.0 bound float2/float3/float4
field loads and stores through normal waves 4/8, reference and ARM64
AOT-object paths. Native import/raising checks all three FP32 vector
widths for both reads and writes, reconstructed modern field layouts,
legacy stride placeholders, 12/16/20-byte strides and field offset 4.
Legacy loads consume complete leading-component runs; AOT supplies an
explicit physical heap, not object-mode binding YAML.

Four complete input records precede a fifth missing its last component.
The last complete record reads correctly; the partly present vector,
upper index 5 and negative-bit-pattern index 0xffffffff read all zero.
Packed writes to those partial/invalid records leave every input word
unchanged. Valid outputs retain every record guard and trailing guard,
including the 12-byte float3 write. Five lanes, divergent float2/float3
copies and fifteen output records per width are checked twice per mode.
All 732 output bytes and all input words are compared exactly.

Copies perform no FP32 arithmetic: negative zero, positive/negative
subnormals, infinities, positive/negative quiet-NaN payloads, adjacent
nonidentical values near one and negative/fractional values retain their
bits across native packed FP32 loads and stores. This is bounded copy
evidence, not a general floating-point arithmetic/conformance claim.

Focused native/raising/CPU packed-buffer LIT passes eight tests with zero
failures in 3.56 seconds; one new fixture adds one test. No compiler or
runtime implementation changed, so no rebuild or broader/full-suite run
was needed. Raw-load admission, legacy partial/coalesced-width limits,
status handling, image/sampling behavior, runtime/SPIR-V code, artifact
ABI 8 and argument layouts remain unchanged. This is native CPU evidence,
not external CTS, an adapter-matrix or owned D3D12 execution. Bound/static
D3D12 descriptors and broader conformance remain open; `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) native raw/structured i32 buffer atomics (2026-11-24):
the new [`native-buffer-atomics.test`](../test/Tools/feme-run/HLSL/native-buffer-atomics.test)
executes actual SDK DXC SM6.6 heap and SM6.0 bound resource atomics through
normal waves 4/8, reference and ARM64 AOT-object paths. Opcode 78 raising
is checked for add, and, or, xor, signed/unsigned min/max and exchange;
opcode 79 raises successful/failed compare-exchange. Existing
`llvm.dx.resource.getpointer` plus monotonic `atomicrmw`/`cmpxchg` and
CPU atomic helpers are reused. Native handle checks retain reconstructed
modern layouts, legacy placeholders, structured stride 12 and field
sub-offsets 4/8. Legacy AOT uses a physical heap, not object binding YAML.

Five lanes each initialize and own their two state words on every repeat.
Exact high-bit/non-FP32-exact old values, signed/unsigned extrema,
UINT_MAX + 1 wrap to zero, successful/failed comparisons and even-lane
increments are checked independently of lane scheduling. Inactive odd
lanes keep the old-value sentinel and leave UINT_MAX unchanged. Upper/
negative-bit-pattern raw addresses and structured indices return zero
for add/compare-exchange without changing valid state or guards.
All words of two guarded 64-byte state buffers and a guarded 744-byte
old-value output are checked in two repeats per mode.

Focused native, retained Clang resource-atomic, DXIL raising and CPU
atomic LIT passes fourteen tests with zero failures in 2.55 seconds;
one new fixture adds one test. No compiler/runtime implementation changed,
so no rebuild or broader/full-suite run was needed. This proves bounded
native i32 buffer atomic execution, not cross-lane/host-thread contention,
memory-order/barrier conformance, typed/image/int64 atomic breadth,
external CTS, an adapter-matrix or owned D3D12. Descriptor materialization,
legacy partial/coalesced-load limits, status handling, images/sampling,
runtime/SPIR-V code, ABI 8 and argument layouts are unchanged.
Bound/static D3D12 descriptors and broader conformance remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) native R32_SINT/R32_UINT typed-buffer atomics (2026-11-24):
the new [`native-typed-buffer-atomics.test`](../test/Tools/feme-run/HLSL/native-typed-buffer-atomics.test)
executes actual SDK DXC SM6.6 heap and SM6.0 bound typed resource atomics
through normal waves 4/8, reference and ARM64 AOT-object paths. All nine
opcode-78 operations are checked across the signed/unsigned descriptors,
along with opcode-79 compare-exchange success/failure. Native raising
preserves signedness in modern scalar handles and legacy atomic-only
four-component fallback handles; existing typed CPU helpers use R32
descriptor formats and element indices, not raw byte offsets.

Five lanes initialize their own signed/unsigned elements using atomic
exchange on every repeat. Exact old values, high-bit/non-FP32-exact
integers, signed/unsigned extrema, UINT_MAX + 1 wraparound and divergent
increments pass independently of scheduling order. Upper index 7 and
negative-bit-pattern index 0xffffffff return zero for add/compare-exchange
on both formats without modifying valid state. Every word of two guarded
28-byte state buffers and a guarded 464-byte old-value output is checked
twice per mode. Legacy AOT supplies a physical heap, not object binding
YAML.

The initial ordinary-store initialization exposed a separate open gap:
SDK DXC SM6.6 emits mask-15 broadcast scalar typed stores, but the typed
store raiser requires its mask width to match the scalar handle width.
Those calls remain explicitly unraised. Atomic exchange initialization
keeps this fixture scoped to atomics; native scalar typed-store admission
and general typed-load/store breadth are not claimed.

Focused native/Clang resource-atomic, DXIL raising and CPU atomic LIT
passes sixteen tests with zero failures in 2.98 seconds; one fixture adds
one test. No implementation changed, so no rebuild or broader/full-suite
run was needed. This is native R32 i32 atomic evidence, not contention,
memory-order/barrier conformance, other-format/image/int64 atomic breadth,
external CTS, an adapter-matrix or owned D3D12. Descriptor materialization,
legacy width inference, raw/structured behavior, status, images/sampling,
runtime/SPIR-V code, ABI 8 and argument layouts remain unchanged.
Bound/static D3D12 descriptors and broader conformance remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) full-mask typed-buffer store admission (2026-11-24):
opcode 69 mask-15 i32/FP32 stores through one/two/three-component typed
handles now retain all four payload channels in a canonical vector store.
This closes the SM6.6 scalar-store gap found by the typed-atomic probe.
The declared handle width and signedness stay intact. Narrowing to a
scalar store would be incorrect: existing scalar helpers fill other
channels with 0/0/1, while the bound descriptor can expose RGBA32 and
native DXC broadcasts the scalar into all four channels. Existing vector
helpers preserve the wire payload and let the bound format choose its
physical texel size. Matching-width stores keep their existing behavior.

All active operand types are checked before constructing IR. Empty,
sparse, dynamic and oversized masks, nonmatching partial masks, mixed
active types and non-32-bit full-mask promotion remain unraised without
partial construction or duplicate writes. Portable raising/CPU tests
check scalar and shorter-vector handles, nonbroadcast component order,
four-channel helper selection, element-index zero-extension, rejected
calls, cleanup and idempotence. The real DirectX forward/raise round-trip
also preserves a scalar handle carrying a four-channel FP32 payload.

The new [`native-scalar-typed-stores.test`](../test/Tools/feme-run/HLSL/native-scalar-typed-stores.test)
executes actual SDK DXC SM6.6 heap and SM6.0 bound scalar SINT/UINT/FP32
assignments against both R32 and RGBA32 descriptors: six format cases
pass normal waves 4/8, reference and ARM64 AOT-object paths. All four
broadcast channels are checked, not only R32 results. Exact signed/high-
bit and adjacent non-FP32-exact integers, negative zero, subnormal,
quiet-NaN payload and negative fractional/infinity bits, divergent stores,
upper/negative-bit-pattern indices, complete texel guards and two repeats
pass. Legacy AOT uses explicit physical heaps, not object binding YAML.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` affected-tool/
CPU-unit build passes 25 steps; changed-line clang-format is clean.
Relevant DXIL/CPU transforms, CPU units, runner and runtime LIT pass
1,720 with 4 unsupported and zero failures in 87.34 seconds; three new
fixtures add three tests. This is bounded native CPU store evidence,
not general partial-mask/other-format/non-32-bit typed closure, a full-
suite, external CTS, adapter-matrix or owned D3D12 result. Loads/status,
atomics, raw/structured behavior, runtime/SPIR-V code, ABI 8 and argument
layouts remain unchanged. Bound/static D3D12 descriptors and broader
conformance remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) native four-component typed-buffer loads (2026-11-24):
the new [`native-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-typed-buffer-loads.test)
executes actual SDK DXC SM6.6 heap and SM6.0 bound `Buffer`/`RWBuffer`
SINT/UINT/FP32 loads through existing opcode-68 raising and vector CPU
helpers. Six R32/RGBA32 formats are exercised as both SRV and UAV views
in normal waves 4/8, reference and ARM64 AOT-object modes. Native raising
checks four-component handle types, signedness and read-only/read-write
classification, with no remaining buffer-load/raw-store calls.

Distinct SRV/UAV inputs at the same logical registers establish namespace
separation. AOT explicitly supplies physical heaps in bound normalization's
space/register/namespace order, interleaving SRVs and UAVs here. RGBA32
inputs have a prefix texel, three complete texels and an incomplete final
texel missing W. All four components of that incomplete CPU view read
zero; upper and negative-bit-pattern indices likewise return all zero.
R32 vector loads instead supply G/B zero and numeric alpha one for valid
elements, distinguishing format defaults from invalid-load results.
Complete texels preserve signed/high-bit and adjacent non-FP32-exact
integers, negative zero, subnormals, infinities and quiet-NaN payloads
without FP32 arithmetic. Five lanes exercise the last complete element,
partial/upper bounds and divergent load/store use. All six input views,
the guarded 584-byte output, inactive-store sentinels and both repeats
are compared exactly: 1,040 bytes per wide case and 680 per narrow case.

Focused native typed loads/stores/atomics, Clang typed-buffer execution,
buffer namespace/kind, DXIL resource raising/round-trip and CPU typed
lowering/normalization LIT pass thirteen tests with zero failures in
4.81 seconds. One fixture adds one test; no implementation changes or
rebuild were needed. No broader/full-suite, external CTS, adapter-matrix,
scalar/shorter-vector or other-format/non-32-bit load closure is claimed.
Partial-texel results establish the shared CPU bounds policy, not owned
D3D12 view construction or conformance. Store admission, status, atomics,
raw/structured behavior, descriptor materialization, runtime/SPIR-V code,
ABI 8 and argument layouts remain unchanged. Bound/static D3D12
descriptors and broader conformance remain open; `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) native scalar/two-component typed-buffer loads (2026-11-24):
the new [`native-narrow-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-narrow-typed-buffer-loads.test)
extends the preceding four-component evidence to actual SDK DXC SM6.6
heap and SM6.0 bound scalar R32 and two-component RG32 SINT/UINT/FP32
SRV/UAV loads. Shader widths match the physical formats. Modern resource
properties and legacy result uses both reconstruct scalar or vector-two
handles with preserved signedness and access class, rather than relying
on a vector-four fallback. Existing scalar/vector-two CPU helpers execute
normal waves 4/8, reference and ARM64 AOT-object paths.

Distinct SRV/UAV inputs at identical logical registers prove namespace
separation. Explicit physical heaps follow space/register/namespace order
for legacy AOT. Five lanes read three complete elements after a prefix
guard, then partial/upper indices. RG32 inputs end with an element missing
Y: both loaded components read zero, not a successful X plus zero Y.
Upper and negative-bit-pattern indices return zero at both widths.
Signed/high-bit and adjacent non-FP32-exact integers, negative zero,
subnormal, positive/negative infinity and quiet-NaN payloads survive
without FP32 arithmetic. Divergent signed UAV copies retain sentinel
values. Every input/output word, prefix/suffix guard and both repeats
pass: 248 bytes per scalar case, 512 bytes per two-component case.

Focused native typed loads/stores/atomics, Clang typed-buffer execution,
buffer namespace/kind, DXIL resource raising/round-trip, CPU typed
lowering/normalization and scalar/vector-two SPIR-V texel-buffer lowering
LIT pass sixteen tests with zero failures in 5.02 seconds. One fixture
adds one test; no implementation changes or rebuild were needed. This
establishes narrow native CPU load/bounds evidence, not three-component,
other-format/non-32-bit typed closure, a broader/full-suite, external CTS,
adapter-matrix or owned D3D12 view construction/conformance result.
Store admission, status, atomics, raw/structured behavior, descriptor
materialization, runtime/SPIR-V code, ABI 8 and argument layouts are
unchanged. Bound/static D3D12 descriptors and broader conformance remain
open; `CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) three-component typed-load lowering (2026-11-24):
`ResourceLoweringPass` now routes three-component i32/FP32 typed loads
through the existing format-aware vector-four helpers, then selects XYZ.
Previously it emitted calls to nonexistent `v3` typed-load helpers.
Physical descriptor formats still determine texel stride and bounds;
widening the runtime result does not force a 16-byte physical access.
The existing image-result narrowing helper is shared with this buffer
path, preserving image behavior without another helper or runtime ABI.
Raw three-component loads and non-32-bit lowering keep their existing
paths; stores, raising, status and handle widths are unchanged.

The new portable
[`resource-lowering-v3-typed-loads.ll`](../test/Transforms/CPU/resource-lowering-v3-typed-loads.ll)
first reproduced the missing-helper path and now checks FP32, signed/
unsigned i32 handles, vector-four helper selection, ordered XYZ shuffles,
element-index zero-extension (including 0xffffffff), cleanup and
idempotence. Raw vector-three and half-vector-three controls retain their
previous lowering; this adds no non-32-bit runtime support.

The new [`native-float3-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-float3-typed-buffer-loads.test)
executes SDK DXC SM6.6 heap and SM6.0 bound FP32 `Buffer<float3>`/
`RWBuffer<float3>` on RGB32 and RGBA32 descriptors, in normal waves 4/8,
reference and ARM64 AOT modes. Native raising retains three-component
handles and SRV/UAV classes. Distinct data at identical logical registers
proves namespace separation; legacy AOT supplies physical heaps.
RGB32 uses a real 12-byte stride; complete RGBA32 texels discard distinct
W values, while a final texel missing W reads all zero even though XYZ
would fit. An incomplete RGB32 texel missing Z likewise reads all zero.
Upper/negative-bit-pattern indices, valid-end loads, divergence, negative
zero, signed subnormals, infinities, quiet-NaN payloads and adjacent FP32
bits pass without arithmetic. Every input word, texel/output guard,
inactive-store sentinel and both repeats are checked: 264 bytes per RGB
case, 304 per RGBA case, including the guarded 152-byte output.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` affected-tool/
CPU-unit build passes 25 steps. Nine focused tests pass in 7.91 seconds;
the resource-lowering/CPU-unit regression selection, including those
tests and raw/non-32-bit controls, passes 244 with zero failures in 31.12
seconds. Two fixtures add two tests. This is portable i32/FP32 lowering
and native FP32 CPU evidence, not native three-component integer,
RGB32-integer/other-format/non-32-bit closure, a broader/full-suite,
external CTS, adapter-matrix or owned D3D12 view/conformance result.
Descriptor materialization, runtime/SPIR-V code, ABI 8 and argument
layouts are unchanged. Bound/static D3D12 descriptors and broader
conformance remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

W2(a)/W2(b) native three-component integer typed loads (2026-11-24):
the new [`native-int3-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-int3-typed-buffer-loads.test)
executes SDK DXC SM6.6 heap and SM6.0 bound `int3`/`uint3` SRV/UAV
loads on RGBA32_SINT/RGBA32_UINT descriptors in normal waves 4/8,
reference and ARM64 AOT-object modes. Modern properties and legacy result
uses retain three-component handles, signedness and access class. Existing
vector-four integer helpers and XYZ selection now have native execution
evidence, not only the preceding portable i32 lowering tests.

Distinct SRV/UAV data at identical logical registers proves namespace
separation; legacy AOT uses space/register/namespace-ordered physical
heaps. Five lanes cover three complete elements after a prefix texel,
a final texel missing W, upper indices and divergent signed UAV copies.
Complete texels discard distinct W values while preserving XYZ order.
The missing-W texel reads all zero even though XYZ fits. Negative-bit-
pattern index 0xffffffff returns zero from all four views. Signed/high-bit,
signed/unsigned extrema and adjacent non-FP32-exact integers survive
without floating conversion. Every word of four 76-byte input views and
the guarded 296-byte output passes twice per mode: 600 compared bytes,
including prefix/texel/output guards and inactive-store sentinels.

Focused native typed loads/stores/atomics, Clang typed-buffer execution,
buffer namespace/kind, DXIL resource raising/round-trip, CPU typed
lowering/normalization and scalar/vector-two SPIR-V texel-buffer lowering
LIT pass nineteen tests with zero failures in 6.19 seconds. One fixture
adds one test without implementation changes or a rebuild. This closes
the preceding native three-component integer evidence gap on RGBA32,
not RGB32-integer decoding, other-format/non-32-bit typed closure,
a broader/full-suite, external CTS, adapter-matrix or owned D3D12
view/conformance result. Raising/lowering policy, stores, status, atomics,
raw/structured behavior, descriptor materialization, runtime/SPIR-V code,
ABI 8 and argument layouts remain unchanged. Bound/static D3D12
descriptors and broader conformance remain open; `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) RGB32 integer read decoding (2026-11-24).**

The shared CPU runtime now recognizes `R32G32B32_UINT` and
`R32G32B32_SINT` as 12-byte texels and copies their three integer words
without floating conversion, supplying alpha one. Existing typed scalar/
vector-two/vector-four loads and integer image loads reuse this decoder.
Typed dimensions count only complete texels; null descriptors remain
unbound. The integer RGB component mask also preserves fixed alpha one
for transparent-black border expansion.

The existing
[`native-int3-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-int3-typed-buffer-loads.test)
now executes RGB32 alongside RGBA32 SINT/UINT with SDK DXC SM6.6 heap
and SM6.0 bound SRV/UAV shaders, normal waves 4/8, reference and ARM64
AOT. Three-component handles still use vector-four helpers/XYZ selection.
Exact signed/high/non-FP32-exact bits, real 12-byte RGB stride, discarded
RGBA W, namespace separation, valid-end loads, divergence, every input/
output word, guards and two repeats pass. Missing RGB Z/RGBA W and upper/
negative indices return all zero: 520 RGB or 600 RGBA compared bytes per
case, including a guarded 296-byte output.

Five new runtime cases cover both integer formats: typed narrow/wide
reads, masked/partial/invalid accesses without dereferencing an unreadable
pointer, complete-element/inactive/unbound dimensions, shared integer
image loads, fixed-alpha borders and unsupported typed-store no-ops.
The affected ARM64 build passes 24 steps; the corrected dimensions fixture
passes a two-step unit rebuild. All 76 selected typed/dimensions/border/
integer-image-load units pass in 16.89 seconds. Nineteen focused native
typed-resource, namespace, raising/round-trip and lowering LIT pass in
12.67 seconds, with zero failures. The existing native fixture is extended,
not a new LIT test.

This closes the read-side RGB32 integer gap only. No RGB32 integer stores
or Vulkan advertisements are added. Raising/lowering policy, SPIR-V code,
status, atomics, raw/structured behavior, descriptor materialization,
ABI 8 and argument layouts remain unchanged. No broader/full-suite,
external CTS, adapter-matrix or owned D3D12/conformance result is claimed.
Bound/static descriptors and broader format support remain open;
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native RGB32 four-component load defaults (2026-11-24).**

The existing
[`native-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-typed-buffer-loads.test)
now executes SDK DXC SM6.6 heap and SM6.0 bound `int4`/`uint4`/
`float4` SRV/UAV loads on RGB32 SINT/UINT/FP32, alongside its R32/
RGBA32 controls. Eight added execution cases cover normal waves 4/8,
reference and ARM64 AOT, each repeated twice. Modern/legacy raising
retains four-component handles and signedness/access class.

RGB32 uses a real 12-byte stride, not the shader result's 16-byte width.
Valid loads preserve exact RGB bits and supply numeric alpha one
(integer 1, FP32 bits 1065353216). Missing-Z texels and upper/negative
index 0xffffffff return all four components zero, including alpha.
Signed/high/non-FP32-exact integers, negative zero, subnormal, positive/
negative infinity and signed quiet-NaN payloads survive without arithmetic.
Distinct SRV/UAV data at identical logical registers proves namespace
separation; legacy AOT uses space/register/namespace-ordered physical
heaps. Valid-end accesses, divergence, every input/output word, guards,
inactive-store sentinels and both repeats pass: six 56-byte input views
plus the guarded 584-byte output, exactly 920 compared bytes per case.

Nineteen focused native typed-resource, namespace, raising/round-trip and
typed/scalar/vector-two lowering LIT pass with zero failures in 6.36
seconds. One existing fixture gains RGB layouts and execution checks,
not a new LIT test. No implementation changes or rebuild were needed.
This validates native RGB32 four-component defaults/bounds after the
preceding shared integer decoder change, not new stores/advertisements,
general format or non-32-bit closure, a broader/full-suite, external CTS,
adapter-matrix or owned D3D12/conformance result. Raising/lowering policy,
runtime/SPIR-V code, status, atomics, raw/structured behavior, descriptor
materialization, ABI 8 and argument layouts remain unchanged.
Bound/static descriptors remain open; `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native narrow RGB32 typed loads (2026-11-24).**

The existing
[`native-narrow-typed-buffer-loads.test`](../test/Tools/feme-run/HLSL/native-narrow-typed-buffer-loads.test)
now executes SDK DXC SM6.6 heap and SM6.0 bound scalar/vector-two
SINT/UINT/FP32 SRV/UAV loads on wider RGB32 descriptors, alongside
matched R32/RG32 controls. Sixteen added execution cases cover both
widths and encodings in normal waves 4/8, reference and ARM64 AOT,
each repeated twice. Modern properties/legacy result uses still recover
narrow handles, signedness and access class.

Both narrow widths retain RGB32's physical 12-byte stride and require
a complete texel. A final texel containing R/G but missing Z returns
zero even though every requested component fits. Valid loads select R
or RG without copying distinct Z values; upper indices and negative-bit-
pattern index 0xffffffff return zero. Exact signed/high/non-FP32-exact
integers, negative zero, subnormal, infinities and signed quiet-NaN
payloads survive without arithmetic. Namespace separation, valid-end
loads, divergent signed UAV copies, every input/output word, guards,
inactive-store sentinels and both repeats pass. Legacy AOT uses physical
space/register/namespace-ordered heaps.

Shared RGB layouts compare six 56-byte input views and a guarded
296-byte raw output, exactly 632 bytes per case at either width. Scalar
execution also checks its entire 37-word trailing sentinel region remains
42; vector-two execution checks the complete output. Nineteen focused
native typed-resource, namespace, raising/round-trip and typed/scalar/
vector-two lowering LIT pass with zero failures in 8.12 seconds.
One existing fixture gains RGB layouts/checks, not a new LIT test;
no implementation changes or rebuild were needed.

This adds native narrow RGB32 read/bounds evidence, not new stores,
Vulkan advertisements, general/non-32-bit format closure, a broader/
full-suite, external CTS, adapter-matrix or owned D3D12/conformance result.
Raising/lowering policy, runtime/SPIR-V code, status, atomics, raw/
structured behavior, descriptor materialization, ABI 8 and argument layouts
remain unchanged. Bound/static descriptors remain open; `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native typed-buffer dimensions (2026-11-24).**

Portable regressions reproduced two missing links: opcode-72
`GetDimensions` only raised texture handles, and CPU buffer lowering
did not recognize canonical typed-buffer dimension queries. The raiser
now maps a typed-buffer query's field-zero element count to
`llvm.dx.resource.getdimensions.x`, with unused/zero mip operands only.
Other fields, mixed supported/unsupported extracts, aggregate uses,
meaningful mips and raw/structured DXIL queries remain unraised. CPU
buffer lowering accepts the scalar typed query and reuses the existing
`feme.cpu.resource.getdimensions.typed.i32` helper, without an element
index, new runtime entry point or argument ABI.

New portable
[`dxil-raise-typed-getdimensions.ll`](../test/Transforms/DXIL/dxil-raise-typed-getdimensions.ll)
and
[`resource-lowering-typed-getdimensions.ll`](../test/Transforms/CPU/resource-lowering-typed-getdimensions.ll)
check scalar FP32, signed three-component UAV and unsigned four-component
handles, descriptor indices, shared-handle query/load lowering, rejection
controls and idempotence. Unsupported raw/xy CPU queries retain the
original function ABI. Existing texture and SPIR-V dimension controls pass.

The new
[`native-typed-buffer-dimensions.test`](../test/Tools/feme-run/HLSL/native-typed-buffer-dimensions.test)
executes SDK DXC SM6.6 heap and SM6.0 bound SINT/UINT/FP32 SRV/UAV
queries in normal waves 4/8, reference and ARM64 AOT. Sixteen execution
cases each repeat twice. RGB32 views sized 32/40/56/20/8/68 bytes
return 2/3/4/1/0/5 complete 12-byte texels; mixed R32/RG32/RGBA32
controls return 8/5/3/5/1/4 from the same bytes and four-component shader
handles. A too-short bound view returns zero, not a fractional element.
Different counts at overlapping SRV/UAV registers prove namespace
separation; legacy AOT uses space/register/namespace-ordered physical
heaps. Divergent queries preserve sentinels. Every input/output word,
guard and both repeats pass: 224 input bytes plus a guarded 128-byte
output, exactly 352 compared bytes per case.

The affected assertions-enabled, PCH-disabled, sccache ARM64 `-j2`
build passes 31 steps. Twenty-five focused native typed-resource,
namespace, raising/round-trip, CPU/SPIR-V lowering and texture-dimension
LIT pass with zero failures in 7.43 seconds. Five existing typed-dimension
runtime units pass in 1.264 seconds, including complete-element counts,
inactive/unbound resources and unreadable-pointer bounds/metadata checks.
Three fixtures add three tests. Changed-line formatting is applied.

This adds typed-buffer dimension raising/CPU lowering and native metadata
evidence only. Runtime/SPIR-V code, loads/stores/status/atomics policy,
format advertisements, descriptor materialization, ABI 8 and argument
layouts remain unchanged. Raw/structured DXIL dimensions, broader format
support, a full suite, external CTS, adapter-matrix and owned D3D12/
conformance closure remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native byte-address buffer dimensions (2026-11-24).**

The native reproduction left `ByteAddressBuffer`/`RWByteAddressBuffer`
opcode-72 queries unraised. Field-zero byte-count queries now raise to
`llvm.dx.resource.getdimensions.x` and lower to the existing
`feme.cpu.resource.getdimensions.raw.i32` helper with literal stride one
and prefix zero. This returns bytes, never DWORDs or structured elements.
Only canonical i8 byte-address handles, field-zero extracts and unused/
zero mip operands are admitted; structured handles, buffer xy fields,
mixed extracts, aggregate uses and meaningful mips remain unsupported.
Typed and texture dimension behavior is preserved.

The existing portable dimension fixtures now check raw SRV/UAV raising,
descriptor indices, byte-count helper arguments, structured/xy rejection,
mixed-field/nonzero-mip rejection and idempotence. The new
[`native-raw-buffer-dimensions.test`](../test/Tools/feme-run/HLSL/native-raw-buffer-dimensions.test)
executes SDK DXC SM6.6 heap and SM6.0 bound SRV/UAV queries in normal
waves 4/8, reference and ARM64 AOT. Eight execution cases each repeat
twice. Sizes 20/28/4/88 return those byte counts, not 5/7/1/22 DWORDs.
Different counts at the same logical SRV/UAV register prove namespace
separation; legacy AOT uses space/register/namespace-ordered physical
heaps. The output handle is both queried and stored through. Divergence,
every input/output word, guards, sentinels and repeats pass: 52 input
bytes plus a guarded 88-byte output, exactly 140 bytes per case.

Two new runtime cases verify an odd 17-byte range returns 17 without
dereferencing an unreadable data pointer; inactive/unbound and invalid
descriptor indices return zero. Existing prefix/stride behavior is also
checked, including zero stride and prefix beyond the range. The affected
assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes 32
steps. Twenty-eight focused native raw/typed-resource, namespace,
raising/round-trip, CPU/SPIR-V lowering, texture-dimension and array-length
LIT pass with zero failures in 7.07 seconds. Seven typed/raw dimension
runtime units pass in 1.329 seconds. One native fixture adds one LIT test;
two portable fixtures are extended and two runtime cases added.

This adds byte-count raising/CPU lowering and native metadata evidence
only. Runtime/SPIR-V code, load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
Structured DXIL count/stride dimensions, broader format support, a full
suite, external CTS, adapter-matrix and owned D3D12/conformance closure
remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native structured-buffer dimensions (2026-11-25).**

The native reproduction confirmed structured opcode-72 element-count
queries remained unraised. They now raise to
`llvm.dx.resource.getdimensions.x` and lower through the existing raw
metadata helper using the handle's declared structure size as stride,
prefix zero. Counts exclude incomplete elements rather than returning
bytes or DWORDs. DXC emits the HLSL stride output as a constant, not a
second DXIL dimension field; this constant is preserved. Query/load
lowering uses the same declared structure-stride calculation.

The existing portable dimension fixtures now check structured SRV/UAV
raising, descriptor indices, 12/20-byte helper strides, query/load
coexistence and idempotence. Buffer xy/mixed-field queries remain
unsupported: a structured HLSL stride is not synthesized from DXIL field 1.
Zero-size structures are rejected by CPU dimension lowering; scalable
or greater-than-32-bit sizes are also outside that lowering's scope.
Unused/zero mip operands remain required, and aggregate uses/meaningful
mips retain the existing rejection policy.

The new
[`native-structured-buffer-dimensions.test`](../test/Tools/feme-run/HLSL/native-structured-buffer-dimensions.test)
executes SDK DXC SM6.6 heap and SM6.0 bound `StructuredBuffer`/
`RWStructuredBuffer` queries in normal waves 4/8, reference and ARM64
AOT. Eight execution cases each repeat twice. Ranges 32/64/8 bytes
with declared strides 12/20/12 return counts 2/3/0 and preserve stride
results 12/20/12. Partial final elements are excluded; a bound view too
short for one element still reports its declared stride. Different
counts/strides at the same logical SRV/UAV register prove namespace
separation; legacy AOT uses space/register/namespace-ordered physical
heaps. Divergence, every input/output word, guards, sentinels and repeats
pass: 104 input bytes plus a guarded 128-byte output, exactly 232 bytes.

A new runtime case verifies complete-element counts without dereferencing
an unreadable data pointer, using the declared stride operand rather than
a deliberately different descriptor stride. Partial/too-short ranges,
SRV/UAV flags, inactive and unbound behavior are checked. The affected
assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build passes 32
steps. Twenty-nine focused native buffer-resource, namespace, raising/
round-trip, CPU/SPIR-V lowering, texture-dimension and array-length LIT
pass with zero failures in 14.08 seconds. Eight dimension runtime units
pass in 1.557 seconds. One native fixture adds one LIT test, two portable
fixtures are extended and one runtime case is added.

This adds structured count raising/CPU lowering and native HLSL count/
stride evidence only. Runtime/SPIR-V code, load/store/status/atomic
policy, advertisements, descriptor materialization, ABI 8 and argument
layouts remain unchanged. Broader format support, a full suite, external
CTS, adapter-matrix and owned D3D12/conformance closure remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native plain-2D texture dimensions (2026-11-25).**

The native reproduction confirmed scalar/pair texture queries raised but
had no CPU lowering; a divergent pair reached SIMDization unchanged and
failed. Heap-backed plain `Texture2D`/`RWTexture2D` FP32/i32 scalar/
vector declarations now lower `getdimensions.x/xy` to the existing CPU
metadata-only width/height helper, without a sampler or derivatives, in
any stage. The scalar form selects width; height-only and paired queries
retain their component selection. Early admission, image-use discovery
and lowering share one predicate, including writable textures and
query/load coexistence. Buffer dimension helpers/policy are unchanged.

The raiser also now refuses nonzero/dynamic texture mips: the scalar/
pair intrinsics carry no mip operand, so accepting these queries used
to silently turn them into mip-zero queries. Unused/zero mip operands
remain accepted. Other result fields, aggregate uses, mip counts and
explicit mip queries remain unraised. Non-heap, array/MS/other shapes
and non-32-bit texture declarations remain outside this CPU increment,
with explicit normal/reference/AOT admission diagnostics. Portable CPU
tests cover scalar/pair lowering, indices, UAV queries, a fragment
query/load dependency, unchanged unsupported functions and idempotence.
Texture raising tests add nonzero/dynamic-mip and mixed-field rejection
plus idempotence; the existing AMDGPU texture-dimension path still passes.

[`native-texture2d-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture2d-dimensions.test)
executes SDK DXC SM6.6 in normal waves 4/8, reference and ARM64 AOT.
Four execution cases each repeat twice. Paired FP32 dimensions report
5x3 from a three-mip image, a UINT UAV width-only query reports 3 and
a SINT SRV height-only query reports 4. A divergent image index makes
lane four unbound, returning zero dimensions and a zero load. Lane one
skips the width store, preserving its sentinel. The full-query handle
also loads an exact FP32 texel. Every word, guard and repeat passes:
128 input bytes plus a guarded 108-byte output, exactly 236 compared
bytes per case. A native dynamic-mip shader rejects in normal/reference/
AOT paths instead of executing a mip-zero substitute.

A new runtime unit verifies metadata-only sampled/storage queries using
an unreadable data pointer, plus inactive/unbound/invalid-index zeros
without dereferencing invalid heaps. The affected assertions-enabled,
PCH-disabled, sccache ARM64 `-j2` build passes 31 steps, followed by a
two-step unit rebuild. Twenty-five focused dimension, texture loading/
sampling, raising, namespace, typed-load and CPU/SPIR-V regressions pass
with zero failures in 21.97 seconds. Nine dimension runtime units pass
in 1.651 seconds. Three new LIT fixtures and one runtime case are added;
one texture-raising fixture is extended.

This adds native mip-zero plain-2D metadata CPU execution only. Runtime/
SPIR-V code, existing load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
Bound/static images, broader shapes/mips/fields, full-suite, external CTS,
adapter-matrix and owned D3D12/conformance closure remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native 1D/array texture dimensions (2026-11-25).**

The native reproduction confirmed 1D/array scalar/pair queries already
raised but failed CPU admission. Heap-backed FP32/i32 scalar/vector
`Texture1D`/`RWTexture1D` and array counterparts now share the existing
dimension admission/lowering predicate. Plain 1D accepts scalar width
only; array 1D accepts width or width/layer count. Existing 1D/array
explicit-LOD metadata helpers receive literal mip zero, without sampler
or derivatives, in any stage. No new runtime helper or ABI is added.
Array layer count is independent of the spatial width and mip chain.

Portable tests cover plain scalar and array scalar/pair lowering,
indices, writable arrays, fragment query-derived layer/load dependence,
handle cleanup and idempotence. Plain 1D pair queries remain unchanged
by lowering and reject explicitly in normal/reference/AOT admission;
the existing non-heap/non-32-bit/2D-array controls still pass. Raising
is unchanged: native dynamic-mip array queries remain unraised and
reject in all three paths rather than executing at mip zero.

[`native-texture1d-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture1d-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT.
Four cases each repeat twice. Three-mip plain/array SRVs report base
widths 5/7, the array SRV reports two layers, a plain UAV reports width
3 and an array UAV reports width 2/three layers. Lane four selects an
unbound array descriptor, returning zero dimensions and a zero load;
lane one skips the plain UAV store, preserving its sentinel. The queried
array also loads exact high-bit signed integer bits from alternating
layers. Every word, guard and repeat passes: 156 input bytes plus a
guarded 148-byte output, exactly 304 compared bytes per case.

Two runtime units check sampled/storage metadata-only queries using
unreadable data pointers, inactive/unbound/zero-mip/invalid-index zeros
and invalid heaps. Helper LOD tests verify shrinking width with fixed
array layers; they are shared-helper controls, not native explicit-mip
support. The affected assertions-enabled, PCH-disabled, sccache ARM64
`-j2` build passes 30 steps. Thirty-four focused dimension, 1D/2D loading/
sampling, raising, namespace, typed-load and CPU/SPIR-V LIT pass with
zero failures in 22.53 seconds. Eleven dimension runtime units pass in
2.659 seconds. Three new LIT fixtures and two runtime cases are added;
the existing 2D admission fixture's diagnostic expectation is updated.

Raising, runtime/SPIR-V code, existing load/store/status/atomic policy,
advertisements, descriptor materialization, ABI 8 and argument layouts
remain unchanged. Other shapes/mips/fields, bound/static images, full-
suite, external CTS, adapter-matrix and owned D3D12/conformance closure
remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native plain-2D explicit-mip dimensions (2026-11-25).**

The native reproduction confirmed explicit-mip/count queries remained
unraised. The existing `llvm.dx.resource.getdimensions.levels.xy`
intrinsic now has symmetric LLVM DXIL forward lowering and FeMe raising:
the i32 mip survives, width/height map to fields 0/1 and DXIL field 3
(total mip count) maps to vector element 2. Sampled plain 2D queries use
this form for meaningful mips or a consumed count, even when only width,
height or count survives. An unused native mip in a count query becomes
zero. Depth fields and aggregate uses remain wholly unraised.

The shared CPU dimension admission/lowering predicate now admits this
form for heap-backed sampled FP32/i32 plain 2D textures with defined
i32 mips, in any stage. Existing explicit-LOD size and total-level
metadata helpers provide the result without sampler or derivatives.
Writable/other-shape/undefined-mip forms retain early normal/reference/
AOT rejection; 1D/array explicit-mip rejection and buffer dimensions are
unchanged. Portable tests cover forward/reverse mapping, live/zero mips,
width-/height-/count-only projections, fragment count-only lowering,
handle cleanup, idempotence and unchanged unsupported functions.

[`native-texture2d-mip-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture2d-mip-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. FP32 7x3/3x1/1x1 and SINT 5x2/2x1/
1x1 extents distinguish three valid dynamic mips while total counts stay
three. Divergent indices make both images unbound in lane four, returning
zero extents/counts/loads. Lane one skips a count store, preserving its
sentinel. Query-derived coordinates load exact negative/high-bit and
non-FP32-exact integer values from three real mips. Every word, guard
and repeat passes: 152 input bytes plus a guarded 128-byte output,
exactly 280 compared bytes. The earlier count-unused explicit-mip variant
now executes rather than rejecting: four additional CPU cases, each
repeated twice, check exactly 236 bytes. Base/mip-zero cases remain.

Two runtime units verify metadata-only mip extents and total counts using
unreadable data pointers, inactive size queries, unbound/zero-mip
descriptors and invalid heaps/indices. The affected assertions-enabled,
PCH-disabled, sccache ARM64 `-j2` build, including `opt`, passes 35 steps.
Thirty-eight focused LLVM forward, FeMe round-trip/raising, dimension,
1D/2D loading/sampling, namespace, typed-load and CPU/SPIR-V LIT pass
with zero failures in 26.63 seconds. Thirteen dimension runtime units
pass in 3.345 seconds. Three new LIT fixtures and two runtime cases are
added; four existing LIT fixtures are extended.

Only valid-mip native results are measured. Shared helper clamping is
unchanged and is not claimed as D3D out-of-range-mip behavior. Runtime/
SPIR-V code, existing load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
Other shapes/fields, bound/static images, full-suite, external CTS,
adapter-matrix and owned D3D12/conformance closure remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native 1D-array explicit-mip dimensions (2026-11-25).**

The native reproduction confirmed sampled 1D-array explicit-mip/count
queries remained unraised. Raising and the shared CPU dimension
admission/lowering predicate now extend `getdimensions.levels.xy` to
heap-backed sampled FP32/i32 1D arrays with defined i32 mips. Width/
layer count use the existing 1D-array size-at-LOD metadata helper; total
mip count uses the shared level helper. DXIL fields 0/1/3 map to vector
elements 0/1/2, preserving the live mip and fixed layer count, without
samplers or derivatives in any stage. Existing LLVM forward lowering is
reused. Writable arrays, plain 1D explicit queries and unsupported shapes/
fields remain rejected; plain 1D base scalar queries remain supported.

[`native-texture1d-array-mip-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture1d-array-mip-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. FP32 widths 7/3/1 with two layers and
three mips and SINT widths 5/2 with three layers and two mips distinguish
layer count from mip count. Query-derived last-X/last-layer loads preserve
exact high-bit/non-FP32-exact integer values from both integer mips.
Divergent indices make both images unbound in lane four, returning zeros.
Lane one skips a count store, preserving its sentinel. Every word, guard
and repeat passes: 172 input bytes plus a guarded 148-byte output,
exactly 320 compared bytes. The earlier count-unused 1D-array variant
now executes rather than rejecting: four additional CPU cases, each
repeated twice, check exactly 304 bytes. Base/mip-zero cases remain.

Portable tests cover 1D-array forward/reverse mapping, live mips,
fragment layer-only queries, helper selection, handle cleanup,
idempotence and unchanged writable-array lowering. Normal/reference/AOT
admission rejects writable-array canonical queries and native plain 1D
explicit queries. The existing array metadata unit additionally checks
mip one yields width three while retaining two layers. The affected
assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build, requesting
`opt` as well as the FeMe tools/runtime tests, passes 15 steps. Forty
focused LLVM/FeMe LIT pass in 28.36 seconds; thirteen dimension runtime
units pass in 2.771 seconds, zero failures. Two LIT fixtures are added;
three existing LIT fixtures and one runtime case are extended.

Only valid-mip native results are measured; shared helper clamping is
unchanged and is not D3D out-of-range-mip evidence. Runtime/SPIR-V code,
existing load/store/status/atomic policy, advertisements, descriptor
materialization, ABI 8 and argument layouts remain unchanged. Other
shapes/fields, bound/static images, full-suite, external CTS, adapter-matrix
and owned D3D12/conformance closure remain open. `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native plain-1D explicit-mip dimensions (2026-11-25).**

The native reproduction confirmed sampled plain-1D explicit queries remained
unraised. A new `llvm.dx.resource.getdimensions.levels.x` intrinsic returns
width/total mip count as two i32 elements. Symmetric LLVM forward lowering
and FeMe raising map DXIL fields 0/3 to vector elements 0/1, preserving
the live mip without inventing a height field. Width-only and count-only
uses are supported; an unused native mip in a count query becomes zero.
Field 1 is not a plain-1D count and leaves the entire native query unraised.
The shared CPU dimension admission/lowering predicate accepts heap-backed
sampled FP32/i32 plain 1D declarations with defined i32 mips, using the
existing 1D size-at-LOD and total-level metadata helpers in any stage
without samplers or derivatives. Writable, undefined-mip, mismatched-shape
and buffer count forms retain early normal/reference/AOT rejection.
Base plain-1D pair queries remain unsupported.

[`native-texture1d-mip-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture1d-mip-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. FP32 widths 7/3/1 with three mips and
SINT widths 5/2 with two mips distinguish live widths from fixed counts.
Query-derived last-X loads preserve exact high-bit/non-FP32-exact integer
values from both integer mips. Divergent indices make both images unbound
in lane four; lane one skips a count store. Every word, guard and repeat
passes: 72 input bytes plus a guarded 108-byte output, exactly 180
compared bytes. The earlier count-unused plain-1D explicit variant now
also executes four CPU cases, each repeated twice, checking 304 bytes.
Base/mip-zero and 1D-array/plain-2D explicit cases remain.

The intrinsic-triggered assertions-enabled, PCH-disabled, sccache ARM64
`-j2` rebuild passes, including `opt`, `llvm-as`, FeMe tools and runtime
tests; a second invocation reports no work. Forty-three focused LLVM
forward, FeMe round-trip/raising, dimension, image, namespace, typed-load
and CPU/SPIR-V LIT pass in 24.10 seconds. Thirteen dimension runtime
units pass in 2.361 seconds, zero failures. Three LIT fixtures are added;
four existing LIT fixtures and one runtime case are extended. Portable
tests verify field-3 mapping, width-/count-only projections, unused-mip
normalization, invalid-field rejection, fragment count-only helper wiring,
idempotence and unchanged unsupported functions. The existing plain-1D
metadata unit additionally checks mip one returns width two from five.

Only valid-mip native results are measured; unchanged shared helper
clamping is not D3D out-of-range-mip evidence. Runtime/SPIR-V code,
existing load/store/status/atomic policy, advertisements, descriptor
materialization, ABI 8 and argument layouts remain unchanged. Other
shapes/fields, bound/static images, full-suite, external CTS, adapter-matrix
and owned D3D12/conformance closure remain open. `CreateDevice` remains
`E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native plain-cube dimensions (2026-11-25).**

The native reproduction confirmed cube base queries raised but explicit-
mip/count queries remained unraised. Raising and the shared CPU dimension
admission/lowering predicate now admit heap-backed sampled FP32/i32 plain
cubes. Scalar/pair base queries use the existing 2D size-at-LOD metadata
helper with literal mip zero; `getdimensions.levels.xy` queries use that
helper with the live mip plus the shared total-level helper. DXIL fields
0/1/3 map to width/height/count elements 0/1/2, not a six-face layer count.
Existing LLVM forward lowering is reused. Queries need no sampler or
derivatives in any stage. Writable cubes, cube arrays and undefined-mip
canonical forms retain early normal/reference/AOT rejection.

[`native-texturecube-dimensions.test`](../test/Tools/feme-run/HLSL/native-texturecube-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT.
The query-only variant runs without a sampler heap; a second variant
uses queried width/count to select +X and reverse three real sampling
mips. Each variant has four cases, each repeated twice. Base 4x4 differs
from valid 4x4/2x2/1x1 mip extents, while a SINT cube supplies 2x2/1x1
with two mips versus three. Divergent indices make both images unbound
in lane four; lane one skips a count store. Every word, guard and repeat
passes: 624 image bytes plus a guarded 168-byte output, exactly 792
compared bytes for query-only. Sampling also checks all 80 sampler bytes,
exactly 872 bytes per case. Sampler output is checked on both repetitions.

The affected assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build
passes 34 steps, including `opt` and `llvm-as`; the runtime unit fix adds
a two-step rebuild. Forty-eight focused LLVM forward, FeMe round-trip/
raising, dimension, cube/1D/2D sampling/loading, namespace, typed-load and
CPU/SPIR-V LIT pass in 43.25 seconds. Fourteen dimension runtime units
pass in 3.675 seconds, zero failures. Three LIT fixtures and one runtime
case are added; the existing forward/reverse round-trip fixture is extended.
Portable tests cover width-/height-/count-only projections, live mip/
helper wiring, fragment queries, idempotence and unchanged writable-cube
lowering. The new metadata unit keeps both JIT entry points before
resolution and uses unreadable texel data to distinguish face extents/
total mip count from six physical layers.

Only valid-mip native results are measured; unchanged helper clamping is
not D3D out-of-range-mip evidence. LLVM intrinsic definitions, runtime/
SPIR-V code, existing load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
Other shapes/fields, bound/static images, full-suite, external CTS,
adapter-matrix and owned D3D12/conformance closure remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native 2D-array dimensions (2026-11-25).**

The native reproduction confirmed full 2D-array queries remained unraised.
New `getdimensions.xyz` and `getdimensions.levels.xyz` intrinsic forms
now have symmetric LLVM forward lowering and FeMe raising. DXIL fields
0/1/2 map to width/height/layers; field 3 is total mip count, a separate
fourth element for explicit queries. Raising supports field-2 layer uses
only for 2D arrays; other depth/layer shapes and aggregate uses remain
outside this slice. An unused native mip in a count query becomes zero.

The shared CPU dimension admission/lowering predicate now accepts
heap-backed FP32/i32 2D-array scalar/pair/triple base SRV/UAV queries and
sampled explicit queries with defined i32 mips. Existing 2D-array size-at-
LOD metadata helpers return fixed layers while shrinking width/height;
the shared level helper appends total mip count. Base queries use mip
zero and narrow scalar/pair results as needed. No sampler or derivatives
are required in any stage. Writable explicit, undefined-mip, mismatched-
shape and buffer triple forms reject early in normal/reference/AOT paths.

[`native-texture2d-array-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture2d-array-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. Base 7x3/layers 2 differs from valid
7x3/3x1/1x1 with three mips. A SINT array supplies 5x2/2x1 with three
layers/two mips; a UINT UAV reports 2x1/layers 4. Query-derived last-X/
Y/layer coordinates load exact high-bit/non-FP32-exact integer values
from both integer mips. Divergent indices make both sampled arrays unbound
in lane four; lane one skips a count store. Every word, guard and repeat
passes: 376 image bytes plus a guarded 308-byte output, exactly 684
compared bytes per case. The bound UAV remains independent of unbound SRVs.

The intrinsic-triggered assertions-enabled, PCH-disabled, sccache ARM64
`-j2` rebuild passes, including `opt`, `llvm-as`, FeMe tools and runtime
tests. Fifty-one focused LLVM forward, FeMe round-trip/raising, dimension,
cube/1D/2D sampling/loading, namespace, typed-load and CPU/SPIR-V LIT pass
in 40.68 seconds. Fifteen dimension runtime units pass in 3.135 seconds,
zero failures. Three LIT fixtures and one runtime case are added; four
existing LIT fixtures are extended or updated for the support change.
Portable tests cover scalar/pair narrowing, sampled/storage queries,
fragment layer-/count-only projections, helper wiring, idempotence,
count normalization and unsupported-function preservation. The metadata
unit uses unreadable texel data to check fixed layers, sampled/storage
queries and inactive/unbound/out-of-range-index zeros.

Only valid native mips are measured; unchanged helper clamping is not
D3D out-of-range-mip evidence. Runtime/SPIR-V code, existing load/store/
status/atomic policy, advertisements, descriptor materialization, ABI 8
and argument layouts remain unchanged. 3D/MS/cube-array dimensions,
bound/static images, full-suite, external CTS, adapter-matrix and owned
D3D12/conformance closure remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native 3D dimensions (2026-11-25).**

FeMe raising and the shared CPU dimension admission/lowering predicate now
accept heap-backed FP32/i32 3D base scalar/pair/triple SRV/UAV queries and
sampled explicit-mip/count queries with defined i32 mips. Existing
`getdimensions.xyz`/`getdimensions.levels.xyz` forms and LLVM forward
lowering are reused: DXIL fields 0/1/2 mean width/height/depth, while
field 3 is total mip count. Count-only queries normalize an unused native
mip to zero. Aggregate uses remain unraised. CPU lowering uses the existing
3D size-at-LOD metadata helper, narrowing base scalar/pair results, and
the shared total-level helper. Unlike 2D-array layers, depth shrinks with
width/height. Queries need no sampler or derivatives in any stage.
Writable explicit, undefined-mip and wrong result-form queries reject
early in normal/reference/AOT paths; cube-array and buffer triple forms
continue to reject.

[`native-texture3d-dimensions.test`](../test/Tools/feme-run/HLSL/native-texture3d-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. FP32 extents 7x3x5/3x1x2/1x1x1 with
three mips and SINT 5x2x3/2x1x1 with two mips distinguish shrinking depth
from fixed layers and total mip count. A UINT UAV reports base 2x1x4.
Query-derived last-X/Y/Z integer loads preserve high-bit/non-FP32-exact
SINT values from both integer mips. Divergent indices make both SRVs
unbound in lane four while the UAV stays bound; lane one skips a count
store. Every word, guard and repeat passes: 608 image bytes plus a guarded
308-byte output, exactly 916 compared bytes per case.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` rebuild passes
in 34 steps, including `opt`, `llvm-as`, FeMe tools and runtime tests.
Sixty focused LLVM forward, FeMe round-trip/raising, dimension, 1D/2D/3D/
cube sampling/loading, namespace, typed-load and CPU/SPIR-V LIT pass in
37.15 seconds. Sixteen dimension runtime units pass in 2.927 seconds,
zero failures. Three LIT fixtures and one runtime case are added; three
existing LIT fixtures are extended or updated for the support change.
Portable checks cover scalar/pair narrowing, fragment depth-/count-only
queries, live mip wiring, round trips, count normalization, idempotence
and unsupported-function preservation. An unreadable-data metadata unit
checks shrinking depth independently of array-layer metadata, storage
base queries and inactive/unbound/zero-mip/bounds zeros.

Only valid native mips are measured; unchanged helper clamping is not
D3D out-of-range-mip evidence. LLVM intrinsic definitions/forward lowering,
runtime/SPIR-V code, load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
MS/cube-array dimensions, bound/static images, full-suite, external CTS,
adapter-matrix and owned D3D12/conformance closure remain open.
`CreateDevice` remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native cube-array dimensions (2026-11-25).**

FeMe raising and the shared CPU dimension admission/lowering predicate now
accept heap-backed sampled FP32/i32 cube-array base scalar/pair/triple
queries and explicit-mip/count queries with defined i32 mips. Existing
`getdimensions.xyz`/`getdimensions.levels.xyz` forms and LLVM forward
lowering are reused. DXIL fields 0/1/2 mean face width/height/cube element
count; field 3 is total mip count. Count-only queries normalize an unused
native mip to zero; aggregate uses remain unraised. CPU lowering uses the
existing cube-array size-at-LOD metadata helper, narrowing base scalar/pair
results, plus the shared level helper. Cube count is fixed `ArrayLayers / 6`,
not physical face count, while face width/height shrink with mip. Queries
need no sampler or derivatives in any stage. Writable cube arrays,
undefined-mip and wrong result-form queries reject early in normal/
reference/AOT paths; buffer triple forms continue to reject.

[`native-texturecube-array-dimensions.test`](../test/Tools/feme-run/HLSL/native-texturecube-array-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
with query-only and sampling variants: four cases each repeated twice
per variant. Eighteen FP32 faces report three cubes, two mips and extents
2x2/1x1; twelve SINT faces report two cubes, one mip and extent 1x1.
The query-only variant has no sampler heap. The sampling variant uses
queried cube count to select the last cube's +X face and queried mip count
to reverse two real mips, returning exact FP32 values. Divergent indices
make both SRVs unbound in lane four; lane one skips a count store. Every
word, guard and repeat passes: 408 image bytes plus a guarded 248-byte
output, exactly 656 compared bytes per query-only case. Sampling also
compares all 80 sampler bytes, totaling 736 bytes per case.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` rebuild passes
in 34 steps, including `opt`, `llvm-as`, FeMe tools and runtime tests.
Sixty-three focused LLVM forward, FeMe round-trip/raising, dimension,
1D/2D/3D/cube sampling/loading, namespace, typed-load and CPU/SPIR-V LIT
pass in 36.30 seconds. Seventeen dimension runtime units pass in 6.502
seconds, zero failures. Three LIT fixtures and one runtime case are added;
two existing LIT fixtures are extended or updated. Portable checks cover
scalar/pair narrowing, fragment cube-/count-only projections, live mip
wiring, round trips, count normalization, idempotence and unsupported
writable-function preservation. An unreadable-data metadata unit checks
fixed cube count versus faces and total mips, shrinking face extents and
inactive/unbound/zero-mip/bounds zeros.

Only valid native mips are measured; unchanged helper clamping is not D3D
out-of-range-mip evidence. LLVM intrinsic definitions/forward lowering,
runtime/SPIR-V code, load/store/status/atomic policy, advertisements,
descriptor materialization, ABI 8 and argument layouts remain unchanged.
MS dimensions, bound/static images, full-suite, external CTS, adapter-
matrix and owned D3D12/conformance closure remain open. `CreateDevice`
remains `E_NOTIMPL`; W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) native 2DMS/array dimensions (2026-11-25).**

The native SDK DXC reproduction confirmed DXIL field 3 is sample count
for both 2DMS shapes; array field 2 is fixed layers. New
`getdimensions.samples.xy`/`getdimensions.samples.xyz` intrinsic forms
return width/height, optional layers and sample count without a mip
operand. Symmetric LLVM forward lowering and FeMe raising preserve those
fields separately from the existing explicit-mip/total-level forms.
Count-unused queries retain x/xy/xyz; nonzero native mips and unsupported
field/aggregate uses remain unraised.

The shared CPU admission/lowering predicate now accepts heap-backed
sampled FP32/i32 2DMS x/xy/samples.xy and 2DMS-array
x/xy/xyz/samples.xyz queries. Existing 2D size, array size-at-LOD (at zero)
and sample-count metadata helpers require no sampler/derivatives in any
stage. Writable/non-heap/non-32-bit and wrong result forms reject early
in normal/reference/AOT paths. Samples-on-non-MS and levels-on-MS are
explicitly rejected rather than conflating sample counts with total mips.

[`native-texturems-dimensions.test`](../test/Tools/feme-run/HLSL/native-texturems-dimensions.test)
executes SDK DXC SM6.6 in waves 4/8, reference and ARM64 AOT,
four cases each repeated twice. FP32 3x2 with four samples and SINT
2x1/layers three with two samples distinguish extents, fixed layers and
sample counts from single-mip metadata. Queried dimensions select the
last X/Y/layer and queried counts select two valid samples, returning
exact FP32 and high-bit/non-FP32-exact SINT values. Divergent indices make
both SRVs unbound in lane four; lane one skips a sample-count store.
Every word, guard and repeat passes: 144 image bytes plus a guarded
188-byte output, exactly 332 compared bytes per case.

The intrinsic-triggered assertions-enabled, PCH-disabled, sccache ARM64
`-j2` rebuild passes, including `opt`, `llvm-as`, FeMe tools and runtime
tests; the final build reports no work. Sixty-eight focused LLVM forward,
FeMe round-trip/raising, dimension, 1D/2D/3D/cube/MS sampling/loading,
namespace, typed-load and CPU/SPIR-V LIT pass in 34.58 seconds. Eighteen
dimension runtime units pass in 3.114 seconds, zero failures. Four LIT
fixtures and one runtime case are added; the LLVM forward fixture is
extended. Portable checks cover scalar/pair narrowing, fragment layer-/
sample-only queries, exact field mappings, round trips, idempotence and
nonzero native mip/invalid field preservation. An unreadable-data metadata
unit varies sample counts 1/2/4/8 independently of extents/layers/mips and
checks inactive/unbound/bounds zeros.

Runtime/SPIR-V code, existing load/store/status/atomic policy,
advertisements, descriptor materialization, ABI 8 and argument layouts
remain unchanged. Bound/static images, non-32-bit/general format and
resolve behavior, full-suite, external CTS, adapter-matrix and owned D3D12/
conformance closure remain open. `CreateDevice` remains `E_NOTIMPL`;
W2(a)/W2(b)/W3(b) are not closed.

**W2(a)/W2(b) floating-point dimension coverage (2026-11-25).**

This is a tested coverage increment, not a new resource capability.
[`native-float-texture-dimensions.test`](../test/Tools/feme-run/HLSL/native-float-texture-dimensions.test)
executes SDK 26100 DXC SM6.6 float-output overloads for plain 2D base/
explicit-mip queries, cube-array mip/cube/level queries, and MS-array
extent/layer/sample queries. Waves 4/8, reference and ARM64 AOT each
repeat twice. All 436 image bytes and the guarded 268-byte output pass,
exactly 704 compared bytes per case, including divergent unbound zeros,
skipped stores and both guards. These SDK overloads emit `sitofp` after
integer queries; raising preserves that native conversion rather than
assuming unsigned conversion from the HLSL dimension domain.

[`dxil-resource-lowering-getdimensions-float.ll`](../test/Transforms/CPU/dxil-resource-lowering-getdimensions-float.ll)
checks scalar signed/unsigned and vector unsigned conversions through
resource lowering and repeated lowering. A metadata-only runtime JIT
case preserves raw width bits and checks distinct `sitofp`/`uitofp`
FP32 bit patterns at seven values, including rounding near 2^24,
`0x7fffffff`, `0x80000000` and `0xffffffff`. An unreadable texel pointer,
unbound data and an out-of-range index confirm no texel access. These
synthetic large widths are conversion tests, not legal native D3D
texture-size or out-of-range-resource evidence.

The assertions-enabled, PCH-disabled, sccache ARM64 `-j2` build and
filtered `check-feme` dependency gate pass: 69 FeMe LIT in 38.07 seconds.
The LLVM dimension forward-lowering fixture passes separately in 0.98
seconds; 19 dimension runtime units pass in 4.974 seconds. No production
compiler/runtime, ABI, argument layout or advertisement changes are
needed. This is not a full-suite or Vulkan/CTS result: the host device
check did not confirm a FeMe ICD, so no Vulkan execution was attempted.
Bound/static images, general format/resolve behavior and owned D3D12
execution remain open; `CreateDevice` stays `E_NOTIMPL` and
W2(a)/W2(b)/W3(b) are not closed.

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
