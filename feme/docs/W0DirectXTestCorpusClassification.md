# W0(c) Direct3D test corpus classification

**Current integration direction (2026-11-06):** container-first application-local
user-mode software driver through stock D3D12/DXGI. The
[integration report](W0WindowsIntegrationProbe.md) supplies a standalone
reference-WARP runner and the next container verification steps. The FeMe
selection helper below remains a design until W0(f)/W1(a) supplies a valid
FeMe adapter; kernel installation is not a prerequisite for starting this work.

This records the classification required by `W0(c)` in
[WARPRoadmap.md](WARPRoadmap.md). It is a research/documentation artifact: no
Direct3D source or test data is copied into this repository, and `C:\dev\
Direct3D` remains a read-only local reference checkout (see its own
`.github\copilot-instructions.md`), not a push target.

| Checkout inspected | Revision |
|---|---|
| `C:\dev\Direct3D` | `1d9469a45630a6114072893da4804511fc179e20` |
| Its `src\D3D12Conf` submodule | `c148818e94859052eb24757bbe7f9ea6c3b3ec16` |

## Categories

### Public black-box tests

Standard-D3D12-API tests that only call public `ID3D12*`/DXGI interfaces
through TAEF, and already support running against an arbitrary adapter
(including WARP, via `/p:Warp=true` for `src\AutoUnitTests`/`src\UnitTests`
or `/p:UseWarp=true /inproc` for `src\D3D12Conf`'s conformance tests, per the
repo's own instructions).

- `src\D3D12Conf\**` (the conformance-test submodule itself, `D3D12Conf`):
  its own `README.md` states it is shared with the IHV-facing `GraphicsHLK`
  repository, not Microsoft-internal-only. This is the strongest public
  black-box category -- it is literally the corpus IHV driver authors run.
  Per-feature subfolders (`BindingTests`, `ResourceTests`, `EnhancedBarriers`,
  `CountersAndQueries`, `IndirectDrawTests`, `RenderPass`, `HeapTests`, etc.)
  exercise only the public API surface.
- `src\AutoUnitTests\**` and `src\UnitTests\**`: TAEF unit tests driving the
  public API (`BasicAPI.cpp`, `ResourceBinding.cpp`, `PipelineStateObject.cpp`,
  `Fence.cpp`, `CommandList.cpp`, `Query.cpp`, ...), with the same
  `/p:Warp=true` adapter-selection hook as the conformance tests.
- `src\d3dtest\func\d3d12\**`: the functional-test framework's D3D12-specific
  test implementations, built on the same public-API harness.

### WARP-private/white-box tests

Tests that call directly into WARP- or SoftGpu-internal interfaces, not
through the public D3D12 API, and therefore cannot be FeMe coverage even in
principle -- they are oracle/reference tests for WARP's own internals.

- `src\warp\tests\**` as a whole: `UMDriverWhitebox`, `ThreadPoolWhitebox`,
  `SIMDJit`, `PixelJIT`, `ShaderUnitTest12`, `D3D10WarpOneCore`, `Conf10`
  (legacy D3D10-era WARP), `BasicRenderOneCore`. Names alone indicate direct
  calls into WARP's rasterizer/JIT/driver internals rather than the D3D12 DDI
  or API surface the design's `W0` sharing contract allows FeMe to target.
- `src\d3dtest\common\SoftGpuApiHelper\**`: a helper for driving a *different*
  internal software GPU ("SoftGpu", distinct from WARP) that "needs to be run
  elevated" with a separately installed kernel driver
  (`%BUILDSHARE%/bin/Graphics/softgpu/InstallSoftGPU.bat`). Any test built on
  this helper is both white-box (talks to a non-public `ISoftGpuApi`/
  `ISoftGpuAdapter` surface) and display/HLK-dependent (see below) -- it is
  not a FeMe target and not a portable black-box oracle either.
- `src\d3dtest\conf\PrivateHeaders\**`: private D3D9/D3D9on12/DXGI internal
  headers (`d3d9p.h`, `d3d9capsp.h`, `dxgkdrt.h`, ...); any test depending on
  these is exercising non-public ABI, not the D3D12 contract.

### DXBC/DXIL input tests

Tests whose inputs are precompiled legacy shader bytecode (DXBC) rather than
DXIL compiled at test build time, relevant to the roadmap's open "mandatory
shader compatibility" scope correction (see `WARPRoadmap.md`'s evaluation
table: "FeMe already imports DXBC; neither that fact nor D3D11-on-12 proves
legacy shader execution").

- `src\AutoUnitTests\D3D11On12.cpp`: exercises the D3D11-on-12 legacy-shader
  compatibility path directly; its `getbins.cmd`/`getbinsfrombuildshare.cmd`
  fetch precompiled binaries rather than compiling shaders from source.
- `src\AutoUnitTests\{BasicAPI,GBVExecuteIndirect,Library,MultiGPU,
  PipelineStateObject,Raytracing,ResourceBinding,ShaderRootSig,util}.cpp`
  and `pch.hpp`: reference DXBC-related symbols/headers (grep-confirmed);
  each needs individual triage for whether it is DXBC-input or merely
  DXBC-adjacent (e.g. format/reflection constants) before being treated as a
  legacy-shader test.
- No current evidence of DXBC inputs inside `src\D3D12Conf` itself (grep for
  `DXBC`/`D3DCompile`/`CompileShader(` found no matches there); its shaders
  appear to be DXIL-only, compiled from HLSL at build time via FXC/DXC.

### Optional/preview features

Tests gated behind the repo's `DeploymentTier` (`Preview`/`Experimental`) or
corresponding to features the roadmap's `W4`/`W6` milestones mark as
separately negotiated, not part of the FL 12_2 / SM 6.5 minimum:

- `src\D3D12Conf\WorkGraphs`, `GenericPrograms`, `PartialGraphicsPrograms`
  (`W6(c)`: Work Graphs and generic programs are explicitly "any selected
  generic programs", a later/optional profile).
- `src\D3D12Conf\SamplerFeedback`, `VariableRateShading`, `ConsRas`
  (conservative rasterization), `DepthBoundsTest`, `GenericPrograms`,
  `GPUUploadHeaps` (`W4(b)`/`W4(c)`'s advanced-graphics/closure tiers).
- `src\D3D12Conf\EnhancedBarriers` (`W6(a)`: enhanced barriers are an
  independently negotiated workstream, not a `W2` prerequisite).
- `src\D3D12Conf\Raytracing` (`W5`'s own scope, already a separate roadmap
  workstream rather than FL 12_2 baseline).

### Display/HLK-dependency tests

Tests that require a real, display-capable host (or a separately installed
kernel driver) and cannot run headlessly in this process-isolated container:

- `src\D3D12Conf\Displayable`, `DirectSR`, `VideoBlit`, and `video`/
  `videodata` content: presentation/flip-model and display-dependent paths,
  matching the roadmap's own `W6(b)` note ("display-capable host" required).
- Anything built on `SoftGpuApiHelper` (see above): requires an elevated
  process plus the separately installed SoftGpu kernel driver.
- `src\D3D12Conf\MultiGPU`, `Multiengine`: typically require specific
  multi-adapter hardware configurations not present in a software-only
  container.

## Source/test redistribution

`C:\dev\Direct3D` and its `D3D12Conf` submodule carry `Copyright (c)
Microsoft Corporation. All rights reserved.` headers throughout (confirmed in
`SoftGpuApiHelper.h` and consistent with the rest of the tree) and are a
Microsoft-internal/IHV-restricted checkout, not an open-source repository.
Per this repository's own instructions (`.github\copilot-instructions.md`,
"Never push to remote without explicit user permission") and the standing
FeMe session rule to treat it as "an architectural and behavioral reference,
not permission to copy Microsoft's implementation into LLVM", **no file,
test name string, shader source, or test data from this checkout may be
vendored, copied, or adapted verbatim into `feme/`**. `D3D12Conf`'s own
`README.md` establishes it is shared more broadly with `GraphicsHLK`, which
somewhat weakens (but does not eliminate) the restriction for that specific
submodule; until FeMe has its own, separately obtained license/distribution
determination for `D3D12Conf`, treat all four categories identically: read
for behavioral understanding, and author new, LLVM-owned tests from the
*public contracts* those tests exercise (the public category's test names
and public D3D12 API calls, not their internal assertions or fixtures).

## W0(d) adapter-selection/identity assertion (design, not yet implemented)

`W0(d)` requires "an explicit FeMe adapter selection/identity assertion to
the future runner." Since no FeMe D3D12 runner exists yet (it depends on
`W0(a)`/`W0(b)` and `W1(a)`'s not-yet-built Windows build target), this
records the design for when one does, rather than implementing it now:

1. A test fixture-level helper, analogous to `SoftGpuApiHelper`'s elevated
   installed-driver check but for FeMe: call `IDXGIFactory::EnumAdapters`/
   `EnumAdapterByLuid`, open each with `D3D12CreateDevice`, and query
   `DXGI_ADAPTER_DESC1`/the opened device's `CheckFeatureSupport` identity
   fields for a FeMe-specific signature (a distinct `VendorId`/`DeviceId`/
   description string FeMe's device-creation path must set once it exists,
   per `W0(a)`'s requirement to "prove discovery, opening, callback
   negotiation, and teardown for a distinct FeMe identity").
2. The helper asserts (not silently skips) if the selected adapter is not the
   expected FeMe identity when a test explicitly requests FeMe -- mirroring
   `src\D3D12Conf`'s `/p:UseWarp=true` opt-in but requiring the assertion
   step the current repo's own WARP-targeting tests do not need (since
   system WARP is a single well-known adapter; a future FeMe installed
   alongside system WARP is not).
3. Record unavailable infrastructure as blocked, not Pass, per `W0(d)`'s own
   acceptance text -- this helper's design intentionally fails loudly rather
   than silently falling back to a different adapter if FeMe identity
   selection cannot be proven.

This design is deliberately conservative: it does not invent a FeMe DXGI
adapter identity ahead of `W0(a)`/`W1(a)` actually creating one, and it does
not attempt to pre-build a runner with no device to run against.
