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

**No W0-W7 milestone is complete.** The branch contains substantial audit,
compiler, reference-oracle, and portable planning work, but no owned D3D12
device or executing FeMe adapter.

| Milestone | Status | Summary |
|---|---|---|
| W0 — Integration feasibility | **Open** | Corpus/DDI audit and internal KMD buffer round trip pass. The opt-in prototype negotiates required tables/caps; executable-shader initialization, application resource lifecycle and runner provenance remain. |
| W1 — Device and headless compute | **Open** | Portable compute, queue, lifetime, and fence plans and CPU/reference shader oracles exist. No owned device, D3D12 queue, resource, PSO, or dispatch. |
| W2 — Binding, resources, synchronization | **Open** | Portable binding/resource/barrier plans and shared image/execution extractions exist. No D3D12 descriptor, allocation/residency, command, or synchronization implementation. |
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
| **W0: Integration feasibility** | Prove the supported Windows runtime/DDI boundary, truthful WARP identity, owned adapter/device lifecycle, and a feasible allocation/resource path. Complete the required test-corpus and shader-compatibility audit. Record exact runtime/SDK configuration, callbacks, identity, and teardown. |
| **W1: Device and headless compute** | Implement the device and object model, queues, allocators/lists, buffers/heaps, fences, and PSOs. Execute resource-free and buffer-writing compute, copy/readback, and fence synchronization against scalar and WARP oracles. |
| **W2: Binding, resources, synchronization** | Implement root signatures, descriptors, resource creation/views/residency, memory safety, copy footprints, command ordering, barriers, queries, and cross-queue synchronization. |
| **W3: Conventional graphics** | Map D3D12 graphics state and stages to the shared executor; validate rasterization, depth/stencil, blending, MSAA, tessellation/geometry, indirect drawing, and off-screen output. |
| **W4: FL 12_2 advanced graphics/resources** | Implement and test mesh/amplification, VRS, sampler feedback, tiled resources, conservative rasterization, and remaining required FL 12_2 limits and formats. |
| **W5: DXR 1.1** | Implement shared acceleration structures, traversal, shader/state-object execution, dispatch, updates, compaction, and the complete selected DXR 1.1 contract. |
| **W6: Presentation and independent options** | Separately complete enhanced barriers, display-capable DXGI presentation/interoperability, and any explicitly selected later shader-model/options profiles. |
| **W7: Product and conformance** | Reconcile every reported capability with focused tests and applicable Microsoft runtime conformance; record runtime identities, skips, failures, performance, and any separate certification scope. |

## Dependencies

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
