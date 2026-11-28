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
pending teardown are tested. The generated v1 inventory is an internal
headless subset, not complete graphics, shader-model or feature-level
conformance. W2-W7 remain open.

| Milestone | Status | Summary |
|---|---|---|
| ~~W0 — Integration feasibility~~ | **Complete** | Supported 0072 boundary, genuine FeMe initialization shaders/pipelines, owned device/build provenance, application KMD buffer lifecycle, and corpus audit are proven. |
| ~~W1 — Device and headless compute~~ | **Complete** | Owned queues/lists/PSOs, grouped DXBC/DXIL dispatch, raw root buffers, copy/readback, monitored fences, pending teardown, exact exports and DXC/Vulkan coexistence; versioned internal capability inventory. |
| W2 — Binding, resources, synchronization | **Open** | Committed buffers, raw SRV/UAV roots, buffer copies/barriers and monitored cross-queue ordering are proven. Complete roots/descriptors/views, images, resource classes, barriers, queries and synchronization semantics remain. |
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
