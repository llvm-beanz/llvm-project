# FeMe WARP implementation roadmap

This is the concise status and dependency overview for the FeMe WARP-like
software adapter. Detailed sub-milestone states, implementation notes, test
results, and historical evidence are in
[WARPImplementationLog.md](WARPImplementationLog.md). The design and selected
product contract are documented in [FeMeWARPDesign.md](FeMeWARPDesign.md).

## Product direction

FeMe targets an application-local, WARP-compatible user-mode driver using
stock Windows D3D12/DXGI, Microsoft's WARP adapter identity, and the existing
unmodified WARP kernel-mode driver. This does not replace system WARP binaries
or promise transparent selection by arbitrary applications.

The target is a complete Feature Level 12_2 software adapter. Capabilities
must only be advertised after implementation and behavioral evidence; the
portable planning library is not a D3D12 runtime.

## Current status

**W0 and W1 are complete.** The compiler-backed application-local UMD creates
an owned stock-runtime device and executes owned DXBC/DXIL compute, buffer
copies/readback, and kernel-ordered asynchronous submissions through the
existing WARP kernel services. Monitored fences, replay, retained code and
pending teardown are tested. W2 adds placed buffers, bounded aliases and
indirect compute dispatch, multiple root CBVs, inline constants and scalar
CBV/raw/structured SRV/UAV tables, finite non-uniform buffer arrays and
R32_UINT/R32_SINT/R32_FLOAT typed buffer views, two/four-component R32 typed
SRVs, and execution-time predication
for direct/indirect dispatch and buffer copies. Structured UAV counter views
execute increment/decrement and bounded uint append/consume through the shared
masked atomic runtime.
Typed SRVs additionally execute checked component mappings, constants and
absent-channel defaults. R8/RGBA8 UINT/SINT/UNORM and R16/RGBA16 UINT/SINT/FLOAT
SRVs also execute packed decoding, mappings and live-view dimension queries.
All 21 typed buffer formats also execute UAV loads/stores, including the
complete mandatory fifteen-format additional set and optional RG32 formats.
Narrow integer stores saturate according to Direct3D rather than truncate.
Root signatures additionally admit disjoint conventional graphics-stage
bindings, without exposing them to compute execution.
Placed buffer-footprint copies additionally preserve independent row/depth
pitches, partial regions, recording snapshots and ordered execution.
Owned single-plane, single-sample linear texture storage now uses that shared
geometry. Mipmapped 3D and 2D-array texture copies pass selected-subresource
indexing, partial regions and held execution; shader views/samplers remain open.
Shared DXIL image/sampler bindings now use independent checked heap prefixes,
qualified SRV/UAV image identity and artifact ABI 12. Native CPU JIT/scalar/AOT
controls distinguish image namespaces and sampler arrays; the Windows image
bridge and owned texture shader views/samplers remain open.
The generated v20 inventory
is an internal headless subset, not complete graphics, shader-model or feature-level
conformance. W2-W7 remain open.

| Milestone | Status | Summary |
|---|---|---|
| ~~W0 — Integration feasibility~~ | **Complete** | Supported 0072 boundary, genuine FeMe initialization shaders/pipelines, owned device/build provenance, application KMD buffer lifecycle, and corpus audit are proven. |
| ~~W1 — Device and headless compute~~ | **Complete** | Owned queues/lists/PSOs, grouped DXBC/DXIL dispatch, raw root buffers, copy/readback, monitored fences, pending teardown, exact exports and DXC/Vulkan coexistence; versioned internal capability inventory. |
| W2 — Binding, resources, synchronization | **In progress** | Committed/placed buffers, raw roots and CBVs, bounded aliases, buffer copies/alias barriers, decreasing monitored fences and dispatch-only indirect execution are proven. Complete roots/descriptors/views, images, resource classes, barriers, queries and other indirect signatures remain. |
| W3 — Conventional graphics | **Open** | Portable graphics-state and draw plans exist; no D3D12 graphics execution or end-to-end rendering. |
| W4 — FL 12_2 advanced graphics/resources | **Open** | Portable capability and mesh-dispatch plans exist; no advanced-feature implementation or conformance evidence. |
| W5 — DXR 1.1 | **Open** | Portable acceleration-structure planning exists; no BVH/ray execution, DXR objects, or ray conformance evidence. |
| W6 — Presentation and modern options | **Open** | Portable enhanced-barrier and presentation contracts exist; no enhanced-barrier execution, DXGI presentation, or modern-profile implementation. |
| W7 — Product/conformance evidence | **Open** | No complete capability profile, applicable Direct3D conformance result, or release evidence. |

Shared extractions WS0-WS3 (image mechanics, task execution, and regression
gates) are recorded as complete in the implementation log. They are
prerequisites, not completed Direct3D milestones. The portable
`FeMeDirect3D` planning library has 65 unit tests; those tests validate plans,
not a device or runtime.

## Milestones and exit criteria

| Milestone | Scope and exit condition |
|---|---|
| **~~W0: Integration feasibility~~** | Completed: supported Windows runtime/DDI boundary, truthful WARP identity, owned adapter/device lifecycle, feasible application resource path, corpus/compatibility audit, and exact configuration/provenance/teardown evidence. |
| **~~W1: Device and headless compute~~** | Completed: owned headless objects and asynchronous compute/copy/fence execution, original DXIL and converted legacy inputs, scalar/system-WARP controls, lifecycle/error gates and versioned subset inventory. |
| **W2: Binding, resources, synchronization** | Implement root signatures, descriptors, resource creation/views/residency, memory safety, copy footprints, command ordering, barriers, queries, and cross-queue synchronization. |
| **W3: Conventional graphics** | Map D3D12 graphics state and stages to the shared executor; validate rasterization, depth/stencil, blending, MSAA, tessellation/geometry, indirect drawing, and off-screen output. |
| **W4: FL 12_2 advanced graphics/resources** | Implement and test mesh/amplification, VRS, sampler feedback, tiled resources, conservative rasterization, and remaining required FL 12_2 limits and formats. |
| **W5: DXR 1.1** | Implement shared acceleration structures, traversal, shader/state-object execution, dispatch, updates, compaction, and the complete selected DXR 1.1 contract. |
| **W6: Presentation and independent options** | Separately complete enhanced barriers, display-capable DXGI presentation/interoperability, and any explicitly selected later shader-model/options profiles. |
| **W7: Product and conformance** | Reconcile every reported capability with focused tests and applicable Microsoft runtime conformance; record runtime identities, skips, failures, performance, and any separate certification scope. |

## Dependencies

W2 remaining work is tracked without deeper milestone nesting:

The shared typed-buffer runtime and Windows bridge 4 execute descriptor-backed
typed SRV component mappings (artifact ABI 12), including post-record updates,
nulls and held execution. Twelve additional narrow SRV formats reuse existing
decoders with exact physical element geometry. All 21 mapped formats execute
typed UAV loads/stores; descriptor-gated saturation preserves existing Vulkan
integer conversion while matching Direct3D's narrow-store contract.

The shared compiler/runtime and Windows bridge now support checked structured
UAV counter views, per-active-lane increment/decrement and bounded uint append/
consume. General payload coverage and dynamically indexed counter-expression parity, plus the other
W2(b) operations, still remain.

| Step | Current boundary and remaining work |
|---|---|
| W2(a) | Raw SRV/UAV roots, multiple CBVs, inline constants and finite CBV/raw/structured tables execute. Disjoint conventional graphics-stage roots coexist while compute prepares only ALL-visible bindings; static samplers, unbounded/general heaps and complete shader/root compatibility remain. |
| W2(b) | Live-owner GPUVA/handle lookup, alias bounds, buffer view creation/copies, execution-time finite/non-uniform array preparation, all 21 mapped typed SRV/UAV formats including the complete additional-format buffer set, typed SRV component mappings/live dimensions, Direct3D narrow-store saturation, structured UAV counters, bounded uint append/consume and null CBV/SRV/UAV views execute; other optional typed/image views, general append/consume payload coverage, volatility, unbounded ranges and complete atomic operations remain. |
| W2(c) | Committed and placed buffers share real WARP allocations and pass alias/copy readback. Uncompressed placed footprints and linear texture storage reuse shared layouts; mipmapped 3D/2D-array copies pass selected-subresource indexing, pitched regions, guards and held lifetime. Texture shader views/samplers, remaining storage formats/planes/MSAA, footprints, reserved/shared resources and broader residency remain. |
| W2(d) | Kernel-ordered copies/direct and indirect dispatch, execution-time 64-bit predication, whole-buffer alias barriers and decreasing fences pass. Complete transitions/promotion/decay/splits, finite ranged barriers, queries, indirect root changes/draws and general removal wakeups remain. |

```text
WS0 -> WS1 --------------------------> W2
WS2 --------------------------------> W1
W0 -> W1 -> W2 -> W3 -> W4 -> W5 -> W7
W0 + W3 ----------------------------> W6 presentation
W2 --------------------------------> W6 enhanced barriers
W5 --------------------------------> W6 later shader/graph profiles
WS3 applies at every extraction and feature gate
```

W0 gates Windows-facing implementation; portable work may proceed in parallel.
W6 workstreams are independently gated as shown. No Vulkan, CPU-oracle, plan
unit-test, or reference-WARP result substitutes for owned FeMe D3D12 execution.

## Completion gates

- Keep the feature inventory tied to specific normative requirements and
  passing tests; do not report FL 12_2 before W1-W5 are complete.
- Validate the changed compiler and runtime boundaries with focused tests,
  then run the declared `check-feme` dependencies on clean builds.
- Test the owned adapter against reference and scalar oracles, and run the
  applicable Direct3D conformance corpus for the exact advertised profile.
- Record environment identity and limitations. Host, VM, and historical
  container results are not interchangeable; installed-driver HLK and
  certification are separate from headless behavioral conformance.

See [WARPImplementationLog.md](WARPImplementationLog.md) for detailed progress
and evidence, and [W0WindowsIntegrationProbe.md](W0WindowsIntegrationProbe.md)
for the Windows integration audit.
