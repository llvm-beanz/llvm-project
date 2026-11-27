# W0(c) Direct3D test corpus classification

**Current integration direction (updated 2026-11-17):** VM-first
application-local user-mode software driver through stock D3D12/DXGI. The
[integration report](W0WindowsIntegrationProbe.md) supplies a standalone
reference-WARP runner and historical container measurements; continue
development and validation in the Windows VM. The FeMe
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
- `src\AutoUnitTests\util.{cpp,h}`: **DXBC-input infrastructure.** Defines
  `GetDefaultVS()`/`GetDefaultPS()`/`GetDefaultCS()`, returning
  `D3D12_SHADER_BYTECODE` views over `g_DefaultVS`/`g_DefaultPS`/`g_DefaultCS`
  -- byte arrays hardcoded directly in the source (grep-confirmed: `g_DefaultPS`
  begins `68, 88, 66, 67` = ASCII `"DXBC"`), not compiled from HLSL at test
  build/run time at all. Also defines the shared `CompileShader()` helper,
  which calls `D3DCompile` (fxc); every caller of it in this file list passes
  a legacy `vs_4_0`/`vs_5_0`/`vs_5_1`/`ps_5_0`/`ps_5_1` profile string, which
  `D3DCompile` always emits as DXBC (it has no SM6.x/DXIL output mode). This
  file is the shared source of DXBC inputs for most of the rest of the list
  below, not merely DXBC-adjacent.
- `src\AutoUnitTests\BasicAPI.cpp`: **DXBC-input, confirmed.** Calls
  `GetDefaultVS()`/`GetDefaultPS()`/`GetDefaultCS()` (hardcoded DXBC blobs,
  above) repeatedly for PSO creation, and one test directly manipulates a
  `DXBCHeader`/`DXBC_HASH_SIZE` (zeroing the container hash) to produce a
  deliberately corrupt shader -- both are direct DXBC-container consumption,
  not merely a DXBC-adjacent constant reference. (Its `DXILLib`/
  `g_TestShaders` raytracing-library use elsewhere in the same file is DXIL,
  not DXBC -- the file mixes both input kinds.)
- `src\AutoUnitTests\GBVExecuteIndirect.cpp`: **DXBC-input, confirmed.** Its
  own local `CompileShader()` helper (distinct from `util.cpp`'s) calls
  `D3DCompile` with `vs_5_0`/`ps_5_0` profiles.
- `src\AutoUnitTests\Library.cpp`: **DXBC-input, confirmed.** Calls
  `D3DCompile` directly; the surrounding comment is explicit ("Use DXBC with
  a different thread-group size from `GetDefaultCS`...").
- `src\AutoUnitTests\MultiGPU.cpp`: **DXBC-input, confirmed.** Calls the
  shared `util.cpp` `CompileShader()` helper with a `vs_5_1` profile.
- `src\AutoUnitTests\PipelineStateObject.cpp`: **DXBC-input, confirmed.**
  Both calls `GetDefaultVS()`/`GetDefaultCS()` (hardcoded DXBC blobs) and
  makes over a dozen direct `D3DCompile` calls, all with legacy
  `vs_4_0`/`vs_5_0`/`ps_5_0`/`ps_5_1` profiles -- the single largest DXBC
  consumer in this list.
- `src\AutoUnitTests\Raytracing.cpp`: **DXBC-input, confirmed (partially).**
  Primarily a DXIL raytracing-library test (`SetDXILLibrary` on
  build-time-embedded DXIL blobs such as `g_TrivialRaytracingUnsigned`), but
  two non-raytracing graphics/compute PSO tests in the same file call
  `GetDefaultVS()`/`GetDefaultCS()` (hardcoded DXBC blobs), so the file as a
  whole is not DXIL-only.
- `src\AutoUnitTests\ResourceBinding.cpp`: **DXBC-input, confirmed, and
  deliberately dual-tested.** Calls `GetDefaultVS()`/`GetDefaultPS()`/
  `GetDefaultCS()` throughout, and explicitly branches on a `bTestDXBCShaders`
  flag to select between the hardcoded DXBC shaders above and separate
  `DefaultVSDXIL`/`DefaultPSDXIL`/`DefaultCSDXIL` DXIL variants -- i.e. this
  file intentionally exercises both legacy DXBC and DXIL inputs side by side
  as a single parameterized test, making it the strongest direct evidence in
  this corpus that WARP's own test suite treats DXBC shader-model support as
  a first-class, deliberately verified compatibility surface.
- `src\AutoUnitTests\ShaderRootSig.cpp`: **DXBC-input, confirmed.** Parses
  hardcoded DXBC blobs (`g_Shader1`/`g_Shader2`) directly with `CDXBCParser`/
  `DXBCGetRootSignature`/`PSVRuntimeInfoFromDXBC`/`DXBCGetShaderFeatureInfo`
  -- this is root-signature-in-container extraction from a real DXBC
  container, not an adjacent constant reference.
- `src\AutoUnitTests\pch.hpp`: **DXBC-adjacent only, not itself DXBC-input.**
  The one file in this list that is merely DXBC-adjacent: it only
  `#include`s `"DXBCUtils.h"` (a precompiled-header aggregator with no test
  logic or shader blob of its own); the actual DXBC consumption happens in
  the `.cpp` files above that use the types it declares.
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
kernel driver) and cannot be validated in a headless VM without display/GPU
passthrough:

- `src\D3D12Conf\Displayable`, `DirectSR`, `VideoBlit`, and `video`/
  `videodata` content: presentation/flip-model and display-dependent paths,
  matching the roadmap's own `W6(b)` note ("display-capable host" required).
- Anything built on `SoftGpuApiHelper` (see above): requires an elevated
  process plus the separately installed SoftGpu kernel driver.
- `src\D3D12Conf\MultiGPU`, `Multiengine`: typically require specific
  multi-adapter hardware configurations not present in a software-only VM.

## Legacy-shader compatibility audit (VM, 2026-11-18)

### Public contract and compiler boundary

Microsoft's [Resource binding in HLSL](https://learn.microsoft.com/en-us/windows/win32/direct3d12/resource-binding-in-hlsl)
explicitly states that all Direct3D 12 hardware supports Shader Model 5.1,
independent of hardware feature level.
[Shader Model 5.1](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-5-1)
lists the six legacy graphics/compute profiles and their descriptor-array
binding model. Thus the selected full D3D12 contract cannot be declared
complete with a DXIL-only application input policy. The roadmap's SM 6.5
baseline adds requirements; it does not remove SM 5.1 compatibility.
The corpus's older SM 4.x/5.0 inputs remain regression targets, not proof by
themselves of which earlier profiles are mandatory.

The four-byte `DXBC` container magic does not distinguish legacy shaders
from DXIL. FeMe's `DXBCImporter` extracts tokenized `SHDR`/`SHEX` shader
parts; `DXILImporter` handles `DXIL`/`ILDB` bitcode instead. DXC's SM 6 output
is therefore not a substitute for testing FXC's legacy compiler output.
The public [shader-creation DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddiarg_create_shader_0026)
documents a shader-code pointer, not a promise that the runtime converts
every legacy input into DXIL for an owned UMD. The actual owned-DDI input
and its import policy still need verification once W1(a) supplies a device;
neither reference WARP nor D3D11-on-12 establishes that boundary for FeMe.

### LLVM-owned real-compiler regression and measured limits

The original shader in
[`fxc-legacy-compute.hlsl`](../test/Translate/DXBC/fxc-legacy-compute.hlsl)
is compiled at test time by the installed Windows SDK FXC as both `cs_5_0`
and `cs_5_1`. It writes a thread-ID-dependent integer expression to a raw
UAV at `u3`; the SM 5.1 variant uses `space7`. LIT checks the decoded
profile/declarations, generated arithmetic/store calls, resource metadata
and compute group dimensions. In particular, range ID 0 must not replace
register 3 or discard register space 7. No compiler blob, Microsoft shader
source or reference-test implementation is checked in.

This exposed and fixed a shared translator bug: `dcl_thread_group` was
decoded but dropped from `!dx.entryPoints`. The translator now emits the
DXIL NumThreads property (tag 4), preserving all three dimensions, and
rejects duplicate declarations explicitly.
[`thread-group-metadata.dxasm`](../test/Translate/DXBC/thread-group-metadata.dxasm)
checks a nontrivial `(16, 8, 3)` group without requiring Windows or FXC;
[`duplicate-thread-group.mlir`](../test/Translate/DXBC/duplicate-thread-group.mlir)
checks the diagnostic.

Validation in the Windows ARM64 VM with SDK 10.0.26100.0:

- Rebuilt `feme-translate` and `feme` with two build jobs.
- LIT DXBC importer/translator suites plus the existing DXBC-to-DXIL CLI
  regression: **144/144 passed**, using native ARM64 FXC.
- The three focused regressions also passed with emulated x64 FXC:
  **3/3 passed**.
- Without FXC on `PATH`, the real-compiler test is correctly **Unsupported**,
  not a pass; the portable metadata tests do not depend on that feature.

Use a Windows SDK developer shell with `fxc.exe` on `PATH`, then run
`python llvm\utils\lit\lit.py -sv -j 2 --timeout=60
build-rel\tools\feme\test\Translate\DXBC
build-rel\tools\feme\test\Import\DXBC
build-rel\tools\feme\test\Tools\feme\feme-dxbc-to-dxil.dxasm`
from the checkout root (one command). Python needs `psutil` for the timeout.

### Legacy raw-buffer DXIL export follow-up (2026-11-20)

The export failure observed on 2026-11-18 was caused by an unraised
resource operation, not the shader's integer arithmetic. DXBC translation emits DXIL 1.0
`BufferStore`/`BufferLoad` (opcodes 69/68) for raw/structured buffers.
The raising pass handled these opcodes only for typed buffers, leaving
the raw store and its `i8` write mask for the backend's integer legalizer.
FeMe now routes legacy raw/structured accesses through the shared scalar
raw-buffer raisers, retaining the existing typed-buffer path and the newer
`RawBufferStore`/`RawBufferLoad` (140/139) encodings.

An unused raw-buffer coordinate is normalized from DXIL's `undef` to the
`poison` required by LLVM's intrinsic; structured-buffer element indices
and byte offsets remain unchanged. The portable raising regression covers
legacy raw and structured loads/stores, float/integer values, a 20-byte
structured stride and nonzero register space. At that stage, multi-
component stores, second-component loads and status reads remained unraised.
The existing lower/raise round-trip now restores its original `poison`
coordinate rather than the previously incorrect `undef`.

Packed-store follow-up (2026-11-24): contiguous two/three/four-component
i32/FP32 stores now raise for both opcode 140 and legacy opcode 69.
Scalar behavior remains; sparse/dynamic/oversized masks, packed non-32-bit
types, mixed active fields and multi-component/status loads remain
unraised. Native SDK DXC SM6.6 heap and SM6.0 bound compute tests verify
guarded packed CPU execution, full-width bounds and exact 12-byte
three-component writes through normal/reference/ARM64 AOT paths.
Legacy AOT supplies a physical heap. This is not a new FXC-profile,
external corpus or owned D3D12 result; see the
[packed-store evidence](W0WindowsIntegrationProbe.md#w2aw3b-packed-rawstructured-dxil-stores-vm-2026-11-24).

Packed-load follow-up (2026-11-24): opcode 139 contiguous masks 1/3/7/15
and legacy opcode 68 complete leading-component result uses now raise
packed i32/FP32 widths two/three/four through existing CPU helpers.
Scalar support and modern explicit widths for partial uses are preserved.
Invalid masks/types, sparse/isolated-later legacy uses, status/aggregate
uses and unused calls remain unraised. Portable raising/CPU regressions
check rewriting, rejected calls, strides/sub-offsets and idempotence.
Native SDK DXC SM6.6 heap and SM6.0 bound compute tests verify exact-bit
`Load2`/`Load3`/`Load4` and structured uint4/float3 fields, divergent uses,
guards, last-valid and all-zero partial/large-address reads, and two
checked repeats through normal waves 4/8, reference and ARM64 AOT.
A separate tail descriptor avoids legacy source-width coalescing; general
legacy partial/coalesced-width semantics remain unproven. Legacy AOT uses
a physical heap. The affected build passes; relevant LIT passes 1,714
with 4 unsupported and zero failures in 74.71 seconds. This is not a new
FXC-profile, external corpus or owned D3D12 result; see the
[packed-load evidence](W0WindowsIntegrationProbe.md#w2aw3b-packed-rawstructured-dxil-loads-vm-2026-11-24).

Native FP32 structured-buffer follow-up (2026-11-24): actual SDK DXC
SM6.6 heap and SM6.0 bound float2/float3/float4 field loads and stores
execute across normal waves 4/8, reference and ARM64 AOT. Strides 12/16/20,
offset 4, exact copies of negative zero/signed subnormals/infinities/
quiet-NaN payloads and adjacent values near one, divergent uses, truncated
final records, invalid indices and guard preservation pass twice.
Partial/invalid reads return all zero; partial/invalid writes preserve
inputs. All 732 output bytes and input words are checked. Legacy loads
use complete leading runs; AOT takes a physical heap. Focused packed-
buffer native/raising/CPU LIT passes eight tests with zero failures in
3.56 seconds; one fixture adds one test without implementation changes
or a rebuild. This is copy/bounds evidence, not a new FXC-profile,
external corpus, general FP32 arithmetic or owned-device result; see the
[native FP32 evidence](W0WindowsIntegrationProbe.md#w2aw3b-native-packed-fp32-structured-buffer-breadth-vm-2026-11-24).

Native buffer-atomic follow-up (2026-11-24): SDK DXC SM6.6 heap and
SM6.0 bound raw/structured i32 shaders execute add/and/or/xor, signed/
unsigned min/max, exchange and successful/failed compare-exchange through
normal waves 4/8, reference and ARM64 AOT. Existing getpointer/atomicrmw/
cmpxchg and CPU helpers preserve stride 12 and field offsets 4/8. Exact
old values, high bits, UINT_MAX + 1 wraparound, divergent increments,
invalid-address zero/no-op behavior and guards pass twice with lane-owned
initialized state. All 64-byte state/744-byte output words are checked;
legacy AOT uses a physical heap. Focused native, retained Clang resource-
atomic, DXIL raising and CPU atomic LIT passes fourteen tests with zero
failures in 2.55 seconds; one fixture adds one test without implementation
changes or a rebuild. This is not a new FXC-profile/external corpus,
contention/memory-order/barrier, typed/image/int64 atomic or owned-device
result; see the
[native atomic evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-rawstructured-i32-buffer-atomics-vm-2026-11-24).

Native typed-atomic follow-up (2026-11-24): SDK DXC SM6.6 heap and SM6.0
bound R32_SINT/R32_UINT shaders check all nine binary operations across
the two formats, successful/failed compare-exchange, exact old values,
wraparound, divergent increments, element-index bounds and guards twice
in normal waves 4/8, reference and ARM64 AOT. Modern scalar and legacy
atomic-only four-component fallback handles retain signedness; atomic
exchange initializes lane-owned state. All guarded 28-byte state/464-byte
output words are checked. Focused native/Clang resource-atomic, DXIL
raising and CPU atomic LIT passes sixteen tests with zero failures in
2.98 seconds; one fixture adds one test without implementation changes
or a rebuild. SM6.6 mask-15 broadcast scalar typed stores remain unraised
due to their mask/handle width mismatch. This is not ordinary typed-load/
store admission, legacy width inference, a new FXC-profile/external
corpus, contention/order/barrier, other-format/image/int64 atomic or
owned-device proof; see the
[typed atomic evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-r32_sintr32_uint-typed-buffer-atomics-vm-2026-11-24).

Full-mask typed-store follow-up (2026-11-24): the preceding scalar-store
gap is closed by preserving mask-15 i32/FP32 payloads through narrower
typed handles as four-channel canonical stores. Declared width/signedness
remain; existing vector helpers avoid scalar helpers' 0/0/1 fills on
wide formats. Invalid/nonmatching partial masks, mixed active types and
non-32-bit promotion stay unraised. Portable raising/CPU and real forward/
raise tests verify width/order, indices, rejected calls and idempotence.
Native SDK DXC SM6.6 heap and SM6.0 bound scalar SINT/UINT/FP32 stores
pass R32/RGBA32 cases across normal waves 4/8, reference and ARM64 AOT
with every broadcast channel, exact bits, divergent stores, invalid
indices, texel guards and two repeats. Legacy AOT takes physical heaps.
The affected ARM64 build passes; relevant LIT passes 1,720 with
4 unsupported and zero failures in 87.34 seconds, adding three fixtures/
tests. This is native CPU store evidence, not new FXC-profile/external
corpus, general typed/partial-mask/non-32-bit closure or owned-device
proof; see the
[full-mask store evidence](W0WindowsIntegrationProbe.md#w2aw2b-full-mask-typed-buffer-stores-vm-2026-11-24).

Native four-component typed-load follow-up (2026-11-24): SDK DXC SM6.6
heap and SM6.0 bound SINT/UINT/FP32 SRV/UAV shaders execute six R32/
RGBA32 formats across normal waves 4/8, reference and ARM64 AOT.
Existing raising/helpers retain access class, signedness and exact bits.
Valid R32 default channels differ from all-zero incomplete/invalid loads.
Distinct inputs at identical logical registers prove namespace separation;
legacy AOT takes space/register/namespace-ordered physical heaps. Valid-
end accesses, divergence, all input/output words, guards and two repeats
pass. Thirteen focused typed-resource/namespace/raising/lowering tests
pass with zero failures in 4.81 seconds; one fixture adds one test without
code changes or a rebuild. This is CPU bounds-policy evidence, not new
FXC-profile/external corpus, D3D12 view construction, general typed closure
or owned-device proof; see the
[native typed-load evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-four-component-typed-buffer-loads-vm-2026-11-24).

Native scalar/vector-two typed-load follow-up (2026-11-24): SDK DXC
SM6.6 heap and SM6.0 bound SINT/UINT/FP32 SRV/UAV shaders execute
matched R32/RG32 formats across normal waves 4/8, reference and ARM64
AOT. Modern properties/legacy uses reconstruct narrow handles; existing
helpers preserve signedness/access class and exact bits. Incomplete RG32
elements return both components zero. Namespace separation, valid-end
accesses, upper/negative bounds, divergence, all input/output words, guards
and two repeats pass; legacy AOT uses space/register/namespace-ordered
physical heaps. Sixteen focused typed-resource/namespace/raising/lowering
tests pass with zero failures in 5.02 seconds; one fixture adds one test
without code changes or a rebuild. This is narrow CPU bounds evidence,
not a new FXC-profile/external corpus, D3D12 view construction or general
typed/owned-device closure; see the
[narrow typed-load evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-scalartwo-component-typed-buffer-loads-vm-2026-11-24).

Three-component typed-load follow-up (2026-11-24): CPU lowering replaces
nonexistent typed `v3` calls with existing vector-four i32/FP32 helpers
and XYZ selection, retaining physical stride/complete-texel bounds.
Portable i32/FP32 regressions and native SDK DXC SM6.6/SM6.0 FP32
SRV/UAV RGB32/RGBA32 cases check 12-byte stride, W discard, incomplete/
invalid all-zero reads, exact bits, namespace separation, valid-end loads,
divergence, guards and two repeats across normal waves 4/8, reference
and ARM64 AOT. The affected ARM64 build passes 25 steps; resource-
lowering/CPU-unit regressions pass 244 with zero failures in 31.12
seconds, including nine focused tests. Two fixtures add two tests. This
is portable i32/FP32 and native FP32 CPU evidence, not native three-
component integer, new FXC-profile/external corpus, general typed-format
or owned-device proof; see the
[three-component evidence](W0WindowsIntegrationProbe.md#w2aw2b-three-component-typed-load-lowering-vm-2026-11-24).

Native integer-three typed-load follow-up (2026-11-24): SDK DXC SM6.6/
SM6.0 `int3`/`uint3` SRV/UAV shaders execute RGBA32 SINT/UINT across
normal waves 4/8, reference and ARM64 AOT. Existing three-component
handles/vector-four helpers/XYZ selection preserve access class,
signedness and exact bits. Distinct-W discard, missing-W/upper/negative
all-zero reads, namespace separation, valid-end loads, divergence, all
input/output words, guards and two repeats pass: 600 compared bytes.
AOT uses explicit physical heaps. Nineteen focused typed-resource/
namespace/raising/lowering tests pass with zero failures in 6.19 seconds;
one fixture adds one test without implementation changes or a rebuild.
This is native RGBA32 integer-three CPU evidence, not RGB32-integer/
general format closure, a new FXC-profile/external corpus or owned-device
proof; see the
[native integer-three evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-three-component-integer-typed-loads-vm-2026-11-24).

RGB32 integer read follow-up (2026-11-24): shared CPU decoding now uses
12-byte RGB32 SINT/UINT texels with exact integer bits and alpha one.
The existing native integer-three fixture adds RGB32 alongside RGBA32
SDK DXC SM6.6/SM6.0 SRV/UAV loads in waves 4/8, reference and ARM64
AOT. Exact bits, physical stride, RGBA W discard, partial/invalid zero
reads, namespaces, divergence, guards and repeats pass: 520 RGB or
600 RGBA compared bytes per case. Five new runtime cases cover typed
narrow/wide reads, dimensions, integer image loads, fixed-alpha borders
and unsupported typed-store no-ops. The affected 24-step ARM64 build and
two-step corrected-unit rebuild pass; 76 selected runtime units pass in
16.89 seconds and nineteen focused LIT pass in 12.67 seconds, with zero
failures. The native fixture is extended, not a new LIT test. No Vulkan
advertisements, stores, raising/lowering policy, SPIR-V code, ABI 8 or
argument layouts change. This is read-side CPU evidence, not general format,
a new FXC-profile/external corpus, external CTS, adapter-matrix or owned-
device proof; see the
[RGB32 integer evidence](W0WindowsIntegrationProbe.md#w2aw2b-rgb32-integer-read-decoding-vm-2026-11-24).

Native RGB32 four-component defaults follow-up (2026-11-24): the existing
native typed-load fixture adds SDK DXC SM6.6/SM6.0 `int4`/`uint4`/
`float4` SRV/UAV loads on RGB32 SINT/UINT/FP32 in normal waves 4/8,
reference and ARM64 AOT. Eight execution cases each repeat twice.
Physical 12-byte stride, exact RGB bits and alpha one versus all-zero
partial/invalid loads, namespaces, divergence, all input/output words,
guards and sentinels pass: exactly 920 compared bytes per case.
Nineteen focused LIT pass with zero failures in 6.36 seconds. The existing
fixture is extended without a new LIT test, implementation changes or
rebuild. No stores, advertisements, raising/lowering policy, runtime/
SPIR-V code, ABI 8 or argument layouts change. This is CPU evidence, not
general format, new FXC-profile/external corpus, external CTS, adapter-
matrix or owned-device closure; see the
[native RGB32 defaults evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-rgb32-four-component-load-defaults-vm-2026-11-24).

Native narrow RGB32 follow-up (2026-11-24): the existing narrow typed-load
fixture adds SDK DXC SM6.6/SM6.0 scalar/vector-two SINT/UINT/FP32
SRV/UAV loads on wider RGB32 descriptors in normal waves 4/8, reference
and ARM64 AOT. Sixteen execution cases each repeat twice. Real 12-byte
stride, missing-Z zero reads even when R/G fit, exact bits, namespaces,
divergence, all input/output words, guards and sentinels pass: exactly
632 compared bytes per case, including a guarded 296-byte output and
the scalar's 37-word trailing sentinel region. Nineteen focused LIT pass
with zero failures in 8.12 seconds. The existing fixture is extended
without a new LIT test, implementation changes or rebuild. No stores,
advertisements, raising/lowering policy, runtime/SPIR-V code, ABI 8 or
argument layouts change. This is CPU evidence, not general format,
new FXC-profile/external corpus, external CTS, adapter-matrix or owned-
device closure; see the
[native narrow RGB32 evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-narrow-rgb32-typed-loads-vm-2026-11-24).

Native typed-buffer dimensions follow-up (2026-11-24): opcode-72 field-zero
queries now raise/lower to the existing CPU typed element-count helper,
with unused/zero mip operands only. Portable tests check handles, indices,
query/load coexistence, rejection and idempotence; unsupported fields,
aggregate uses, meaningful mips and raw/structured DXIL queries remain
unraised. Native SDK DXC SM6.6/SM6.0 SINT/UINT/FP32 SRV/UAV queries
pass RGB32 and mixed R32/RG32/RGBA32 in waves 4/8, reference and ARM64
AOT, with sixteen execution cases each repeated twice. Physical complete-
element counts, partial/too-short views, namespaces, divergence, all input/
output words, guards and sentinels pass: exactly 352 bytes per case.
The affected 31-step ARM64 build passes; 25 focused LIT pass in 7.43
seconds and five existing runtime units pass in 1.264 seconds, with zero
failures. Three fixtures add three tests. Runtime/SPIR-V code, load/store/
status/atomic policy, advertisements, ABI 8 and argument layouts remain.
This is native CPU metadata evidence, not new FXC-profile/external corpus,
external CTS, adapter-matrix or owned-device closure; see the
[typed-buffer dimension evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-typed-buffer-dimensions-vm-2026-11-24).

Native byte-address dimensions follow-up (2026-11-24): opcode-72
field-zero queries now raise/lower through the existing raw helper with
stride one/prefix zero, returning bytes, not DWORDs or structured elements.
Structured/xy queries, mixed/aggregate uses and meaningful mips remain
unsupported. Extended portable fixtures check raw SRV/UAV handles,
indices, rejection and idempotence. Native SDK DXC SM6.6/SM6.0 queries
execute in waves 4/8, reference and ARM64 AOT with eight cases each
repeated twice. Counts 20/28/4/88, namespaces, divergence, output query/
store coexistence, all words, guards and sentinels pass: exactly 140 bytes.
The affected 32-step ARM64 build passes; 28 focused LIT pass in 7.07
seconds and seven runtime dimension units pass in 1.329 seconds, with
zero failures. One native LIT fixture and two runtime cases are added;
two portable fixtures are extended. Runtime/SPIR-V code, load/store/
status/atomic policy, advertisements, ABI 8 and argument layouts remain.
This is native CPU metadata evidence, not new FXC-profile/external corpus,
external CTS, adapter-matrix or owned-device closure; see the
[byte-address dimension evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-byte-address-buffer-dimensions-vm-2026-11-24).

Native structured-buffer dimensions follow-up (2026-11-25): field-zero
counts now raise/lower through the existing raw metadata helper using
declared structure size as stride, prefix zero. DXC's constant HLSL stride
output is preserved, not treated as DXIL field 1. Buffer xy/mixed fields,
aggregate uses and meaningful mips remain unsupported. Portable SRV/
UAV, index, query/load, zero-size rejection and idempotence controls pass.
SDK DXC SM6.6/SM6.0 queries execute in waves 4/8, reference and ARM64
AOT with eight cases repeated twice. Ranges 32/64/8 and strides
12/20/12 yield counts 2/3/0; partial/too-short views, namespaces,
divergence, all words, guards and sentinels pass: exactly 232 bytes.
The affected 32-step ARM64 build passes; 29 focused LIT pass in 14.08
seconds and eight runtime units pass in 1.557 seconds, with zero failures.
One native LIT fixture and one runtime case are added; two portable
fixtures are extended. Runtime/SPIR-V code, load/store/status/atomic
policy, advertisements, ABI 8 and argument layouts remain unchanged.
This is native HLSL count/stride CPU evidence, not new FXC-profile/
external corpus, external CTS, adapter-matrix or owned-device closure;
see the [structured-buffer dimension evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-structured-buffer-dimensions-vm-2026-11-25).

Native plain-2D texture dimensions follow-up (2026-11-25): heap-backed
FP32/i32 scalar/pair queries now lower to the existing CPU metadata
helper in any stage without sampler/derivatives. Width-only, height-only
and paired queries share admission/lowering eligibility; nonzero/dynamic
texture mips now stay unraised, not discarded. SDK DXC SM6.6 passes
waves 4/8, reference and ARM64 AOT, four cases repeated twice. Distinct
extents, a three-mip base query, divergent/unbound indices, query/load
coexistence, all words, guards and sentinels pass: exactly 236 bytes.
The affected 31-step ARM64 build and two-step unit rebuild pass; 25
focused LIT pass in 21.97 seconds and nine dimension units in 1.651
seconds, zero failures. Three LIT fixtures and one runtime case are
added; one raising fixture is extended. Runtime/SPIR-V code, existing
load/store/status/atomic policy, advertisements, ABI 8 and layouts remain
unchanged. Bound/static images, broader shapes/mips/fields, new FXC-
profile/external corpus, external CTS, adapter-matrix and owned-device
closure remain open; see the [plain-2D dimension evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-plain-2d-texture-dimensions-vm-2026-11-25).

Native 1D/array texture dimensions follow-up (2026-11-25): heap-backed
FP32/i32 SRV/UAV scalar/pair queries now share dimension admission/
lowering through existing metadata helpers with literal mip zero in any
stage, without sampler/derivatives. Plain 1D accepts width; array 1D accepts
width or width/layer count. Plain 1D pairs and native explicit mips retain
rejection. SDK DXC SM6.6 passes waves 4/8, reference and ARM64 AOT,
four cases repeated twice. Three-mip base widths, distinct layer counts,
divergent/unbound indices, exact integer query/load results, all words,
guards and sentinels pass: exactly 304 bytes. The affected 30-step ARM64
build passes; 34 focused LIT pass in 22.53 seconds and eleven dimension
units in 2.659 seconds, zero failures. Three LIT fixtures and two runtime
cases are added; the existing 2D admission diagnostic expectation is updated.
Raising, runtime/SPIR-V code, existing load/store/status/atomic policy,
advertisements, ABI 8 and layouts remain unchanged. Other shapes/mips/
fields, bound/static images, new FXC-profile/external corpus, external CTS,
adapter-matrix and owned-device closure remain open; see the
[1D/array dimension evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-1darray-texture-dimensions-vm-2026-11-25).

Native plain-2D explicit-mip dimensions follow-up (2026-11-25): the
existing `getdimensions.levels.xy` intrinsic now has LLVM forward/FeMe
reverse lowering, retaining live i32 mips and mapping DXIL count field
3 to vector element 2. Heap-backed sampled plain 2D FP32/i32 queries
reuse existing CPU size/level metadata helpers without sampler/derivatives,
in any stage. Writable/other-shape/undefined-mip forms reject; depth/
aggregate uses remain unraised. SDK DXC SM6.6 passes waves 4/8,
reference and ARM64 AOT, four cases repeated twice, with distinct valid
mip extents, fixed counts, divergent/unbound indices, query-derived exact
integer loads, all words, guards and sentinels: exactly 280 bytes. A prior
count-unused variant adds four execution cases, repeated twice, checking
236 bytes. The affected 35-step ARM64 build passes; 38 focused LLVM/
FeMe LIT pass in 26.63 seconds and thirteen dimension units in 3.345
seconds, zero failures. Three LIT fixtures and two runtime cases are added;
four LIT fixtures are extended. Shared helper clamping, runtime/SPIR-V
code, existing load/store/status/atomic policy, advertisements, ABI 8 and
layouts remain unchanged. Out-of-range-mip native semantics, other
shapes/fields, bound/static images, new FXC-profile/external corpus,
external CTS, adapter-matrix and owned-device closure remain open; see
the [plain-2D explicit-mip evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-plain-2d-explicit-mip-dimensions-vm-2026-11-25).

Native 1D-array explicit-mip dimensions follow-up (2026-11-25): raising
and shared CPU admission/lowering now extend `getdimensions.levels.xy`
to heap-backed sampled FP32/i32 1D arrays with defined i32 mips.
Width/layers/total mip count preserve DXIL fields 0/1/3 and the live mip
through existing 1D-array size/level helpers, without sampler/derivatives
in any stage. Existing LLVM forward lowering is reused. Writable arrays
and plain 1D explicit queries remain unsupported. SDK DXC SM6.6 passes
waves 4/8, reference and ARM64 AOT, four cases repeated twice, for
widths 7/3/1, two layers/three mips and widths 5/2, three layers/two
mips. Query-derived exact SINT loads, divergent/unbound indices, skipped-
store sentinels, every word, guards and repeats pass: exactly 320
compared bytes. The prior count-unused variant adds four execution
cases, repeated twice, checking 304 bytes each. The affected 15-step
ARM64 build passes; forty focused LLVM/FeMe LIT pass in 28.36 seconds
and thirteen dimension units in 2.771 seconds, zero failures. Two LIT
fixtures are added; three LIT fixtures and one runtime case are extended.
Only valid-mip native results are measured; shared helper clamping,
runtime/SPIR-V code, load/store/status/atomic policy, advertisements,
ABI 8 and layouts remain unchanged. Other shapes/fields, bound/static
images, new FXC-profile/external corpus, external CTS, adapter-matrix
and owned-device closure remain open; see the
[1D-array explicit-mip evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-1d-array-explicit-mip-dimensions-vm-2026-11-25).

Native plain-1D explicit-mip dimensions follow-up (2026-11-25): new
`getdimensions.levels.x` returns width/total mip count, with symmetric
LLVM forward lowering and FeMe raising mapping DXIL fields 0/3 to
vector elements 0/1. Heap-backed sampled FP32/i32 CPU queries with
defined i32 mips reuse existing size/level metadata helpers in any stage
without sampler/derivatives. Writable/undefined-mip and mismatched-shape
canonical forms reject; base 1D pairs remain unsupported. Native SDK DXC
SM6.6 passes waves 4/8, reference and ARM64 AOT, four cases repeated
twice, with widths 7/3/1 and three mips versus widths 5/2 and two mips.
Query-derived exact SINT loads, divergent/unbound indices, skipped stores,
every word, guards and repeats pass: exactly 180 compared bytes.
The count-unused plain-1D variant adds four cases repeated twice,
checking 304 bytes each. See the
[plain-1D explicit-mip evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-plain-1d-explicit-mip-dimensions-vm-2026-11-25)
for validation. Only valid native mips are measured; shared helper clamping,
runtime/SPIR-V code, load/store/status/atomic policy, advertisements,
ABI 8 and layouts remain unchanged. Other shapes/fields, bound/static
images, new FXC-profile/external corpus, external CTS, adapter-matrix
and owned-device closure remain open.

Native plain-cube dimensions follow-up (2026-11-25): raising and shared
CPU admission/lowering now admit heap-backed sampled FP32/i32 cubes.
Base scalar/pair and explicit width/height/count queries reuse existing
2D size-at-LOD and total-level metadata helpers in any stage without
sampler/derivatives. Live mips and DXIL count field 3 survive through
`getdimensions.levels.xy`; existing LLVM forward lowering is reused.
Native SM6.6 passes waves 4/8, reference and ARM64 AOT in sampler-free
and query-derived sampling variants, four cases each repeated twice.
Base/three valid face extents, distinct counts, divergence/unbound zeros,
skipped stores, every word, guards and repeats pass: 792 bytes for
query-only, 872 with all sampler bytes. The affected 34-step ARM64 build
and two-step unit rebuild pass; 48 focused LLVM/FeMe LIT pass in 43.25
seconds and fourteen dimension units in 3.675 seconds, zero failures.
Three LIT fixtures and one runtime case are added; the round-trip fixture
is extended. Writable cubes, cube arrays and undefined-mip canonical
forms reject. Only valid native mips are measured; helper clamping,
LLVM intrinsic definitions, runtime/SPIR-V code, load/store/status/atomic
policy, advertisements, ABI 8 and layouts remain unchanged. Other
shapes/fields, bound/static images, new FXC-profile/external corpus,
external CTS, adapter-matrix and owned-device closure remain open; see
the [plain-cube evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-plain-cube-dimensions-vm-2026-11-25).

Native 2D-array dimensions follow-up (2026-11-25): new xyz/levels.xyz
intrinsic forms map width/height/layers and optional total mip count
through symmetric LLVM forward lowering and FeMe raising. Heap-backed
FP32/i32 base SRV/UAV and sampled explicit CPU queries reuse existing
array size/level metadata helpers in any stage without sampler/
derivatives. Layers stay fixed as mip extents shrink. Native SM6.6
passes waves 4/8, reference and ARM64 AOT, four cases repeated twice,
distinguishing base/mip extents, layers two/three/four and total mip counts
three/two. Query-derived exact SINT loads, divergent/unbound indices,
skipped stores, every word, guards and repeats pass: exactly 684 compared
bytes. The intrinsic-triggered ARM64 rebuild passes; 51 focused LLVM/
FeMe LIT pass in 40.68 seconds and fifteen dimension units in 3.135
seconds, zero failures. Three LIT fixtures and one runtime case are added;
four LIT fixtures are extended or updated. Writable explicit, undefined-
mip and mismatched-shape canonical forms reject; 3D/MS/cube-array
dimensions remain unsupported. Only valid native mips are measured;
helper clamping, runtime/SPIR-V code, load/store/status/atomic policy,
advertisements, ABI 8 and layouts remain unchanged. Bound/static images,
new FXC-profile/external corpus, external CTS, adapter-matrix and owned-
device closure remain open; see the
[2D-array evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-2d-array-dimensions-vm-2026-11-25).

Native 3D dimensions follow-up (2026-11-25): FeMe raising and shared CPU
admission/lowering now support heap-backed FP32/i32 base x/xy/xyz SRV/
UAV queries and sampled levels.xyz with defined i32 mips. Existing
intrinsic forms and 3D size/level metadata helpers preserve shrinking
width/height/depth and a separate total mip count, without sampler/
derivatives in any stage. Native SM6.6 passes waves 4/8, reference and
ARM64 AOT, four cases repeated twice, checking all 916 bytes,
query-derived exact SINT loads, divergent/unbound indices, skipped stores
and guards. The 34-step ARM64 rebuild passes; sixty focused LLVM/FeMe
LIT pass in 37.15 seconds and sixteen dimension units in 2.927 seconds,
zero failures. Writable explicit, undefined-mip and wrong result-form
queries reject. Only valid native mips are measured; LLVM intrinsic
definitions/forward lowering, runtime/SPIR-V code, helper clamping,
load/store/status/atomic policy, advertisements, ABI 8 and layouts remain
unchanged. MS/cube-array dimensions, bound/static images, new FXC-profile/
external corpus, external CTS, adapter-matrix and owned-device closure
remain open; see the
[3D evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-3d-dimensions-vm-2026-11-25).

Native cube-array dimensions follow-up (2026-11-25): FeMe raising and
shared CPU admission/lowering support heap-backed sampled FP32/i32 base
x/xy/xyz and levels.xyz queries with defined i32 mips. Existing forms and
cube-array size/level metadata helpers preserve face width/height, fixed
cube element count (`ArrayLayers / 6`) and total mip count, without
sampler/derivatives in any stage. Native query-only and sampling variants
pass waves 4/8, reference and ARM64 AOT, four cases repeated twice per
variant. Eighteen/twelve faces report three/two cubes independently of
two/one mip counts. Queried counts select the last cube and reverse two
real mips; every word, divergence/unbound zeros, skipped stores, guards
and repeats pass: 656/736 compared bytes. The 34-step ARM64 rebuild
passes; sixty-three focused LLVM/FeMe LIT pass in 36.30 seconds and
seventeen dimension units in 6.502 seconds, zero failures. Writable cube
arrays, undefined mips and wrong result forms reject. Only valid native
mips are measured; LLVM intrinsic definitions/forward lowering, runtime/
SPIR-V code, helper clamping, load/store/status/atomic policy,
advertisements, ABI 8 and layouts remain unchanged. MS dimensions,
bound/static images, new FXC-profile/external corpus, external CTS,
adapter-matrix and owned-device closure remain open; see the
[cube-array evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-cube-array-dimensions-vm-2026-11-25).

Native 2DMS/array dimensions follow-up (2026-11-25): new samples.xy/xyz
intrinsic forms and symmetric forward/reverse mapping preserve width/
height, optional fixed layers and sample count without a mip operand.
DXIL field 3 is sample count, not total mips. Shared CPU admission/lowering
accepts heap-backed sampled FP32/i32 queries through existing size/sample-
count metadata helpers, without sampler/derivatives in any stage. Native
SM6.6 passes waves 4/8, reference and ARM64 AOT, four cases repeated
twice, checking all 332 bytes, query-derived exact FP32/SINT sample loads,
divergence/unbound zeros, skipped stores and guards. The intrinsic-
triggered ARM64 rebuild passes; sixty-eight focused LLVM/FeMe LIT pass
in 34.58 seconds and eighteen dimension units in 3.114 seconds, zero
failures. Writable/non-heap/non-32-bit, wrong result forms, samples-on-
non-MS and levels-on-MS queries reject. Runtime/SPIR-V code, existing
load/store/status/atomic policy, advertisements, ABI 8 and layouts remain
unchanged. Bound/static images, non-32-bit/general format and resolve
behavior, new FXC-profile/external corpus, external CTS, adapter-matrix
and owned-device closure remain open; see the
[2DMS/array evidence](W0WindowsIntegrationProbe.md#w2aw2b-native-2dmsarray-dimensions-vm-2026-11-25).

The original FXC shader now exports successfully as DXIL for both legacy
profiles. The new
[`feme-fxc-dxbc-to-dxil.test`](../test/Tools/feme/feme-fxc-dxbc-to-dxil.test)
reuses the original HLSL and re-imports the emitted containers, verifies the
LLVM IR, and checks the stored integer expression, byte addressing, scalar
write mask, register/space and `(8, 1, 1)` thread group. The portable
[`feme-dxbc-raw-to-dxil.dxasm`](../test/Tools/feme/feme-dxbc-raw-to-dxil.dxasm)
also exports and verifies a raw-buffer load/store without requiring FXC.
This is stronger than checking only the container magic, but is not
execution or admission by the Windows runtime/DXIL validator.

Validation: the modified `feme`/`feme-opt` consumers built successfully.
The combined LIT DXIL-raising, DXBC-import/translation, typed/raw DXIL
round-trip and CPU raw-resource regressions passed **171/171** with native
ARM64 FXC. The three focused raising/export tests also passed **3/3**
with emulated x64 FXC from SDK 10.0.26100.0. Both runs used two workers
and 60-second per-test timeouts.
Without FXC on `PATH`, the two portable focused tests pass and the two
real-FXC import/export tests are explicitly **Unsupported**, not counted
as passes.

| Remaining compatibility work | Existing roadmap gate |
|---|---|
| Extend the scoped [reference compute oracle](W0WindowsIntegrationProbe.md#w1b-legacy-grouped-compute-follow-up-vm-2026-11-20) beyond fixed linear/grouped workloads. Both legacy profiles now execute as FXC DXBC and SDK-validated FeMe DXIL, including TGSM exchange and three group barriers, on reference WARP; bare DXIL export remains rejected. Nonstandard barrier modes, wave policy and lifetime coverage remain open. This is not owned-device execution or D3D12 conformance. | W0(d), W1(b) |
| Observe the owned shader DDI inputs, wire the supported legacy import route into PSO creation, and execute/read back this shader against scalar expectations and reference WARP with device/build provenance. | W0(g), W1(a), W1(b) |
| Exercise descriptor arrays, dynamic/non-uniform indices and root-layout binding with real SM 5.1 compiler output, beyond this one fixed raw-UAV binding. | W2(a), W2(b) |
| Extend real-compiler coverage to all six legacy stages, signatures, stage-specific declarations and operations, then validate translated execution. Existing assembler/import tests are not end-to-end shader support. | W1, W3, W4 |

W0(c)'s corpus classification, redistribution review, mandatory SM 5.1 input
scope, and LLVM-owned focused public-contract tests are complete. This does
not establish full all-stage legacy coverage, successful execution through an
owned D3D12 device, or compliance; those implementation gates remain in W1-W4.

### Legacy compute runtime admission follow-up (2026-11-20)

The public reference-WARP oracle now verifies 256 scalar results and all
64 KiB of output/guard data through both adapter entry paths, for
`cs_5_0`/`cs_5_1` DXBC and FeMe-translated DXIL. The SDK validator exposed
and helped fix leaked private signature metadata and an incorrect
empty-signature compute PSV ABI version. Corrected DXIL must still pass
through the public SDK validator to obtain a runtime-admitted container:
unvalidated export is explicitly tested as PSO rejection, not execution.

All **40/40** LIT cases pass on native ARM64 inbox, emulated x64 inbox
and native ARM64 Agility 619; the no-tools default passes **28/28** without
counting absent optional dispatch cases. Related LLVM/container and FeMe
compiler/backend regressions pass **289**, with **1 Unsupported**.
See the [integration evidence](W0WindowsIntegrationProbe.md#w0d-legacy-compute-dispatchreadback-oracle-vm-2026-11-20)
for exact tools, module provenance, negative controls and reproduction.
No owned device, shader-DDI or broader legacy support is implied.

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

## W0(d) adapter-selection and module-provenance assertion (future-runner design)

`W0(d)` requires explicit selection and identity evidence for the future
runner. The selected product intentionally shares Microsoft's WARP identity,
so adapter IDs alone cannot distinguish FeMe from system WARP. Since no FeMe
D3D12 runner exists yet (it depends on `W0(a)`/`W0(g)` and `W1(a)`'s
not-yet-built Windows target), this records the design rather than
implementing it now:

1. A test fixture-level helper, analogous to `SoftGpuApiHelper`'s elevated
   installed-driver check, selects the adapter through the chosen
   application-local WARP route and opens it with `D3D12CreateDevice`. Assert
   Microsoft's WARP identity (`VendorId=0x1414`, `DeviceId=0x008c`, software
   flag) on both the enumerated adapter and created device.
2. Independently prove that the loaded user-mode driver is the FeMe build
   selected for the test, not the system WARP module. Record and assert the
   expected application-local module path and a stable build/package
   provenance value; the shared WARP IDs and description are not provenance.
3. If a test explicitly requests FeMe, fail rather than silently fall back to
   system WARP when either identity or module provenance differs. This mirrors
   `src\D3D12Conf`'s `/p:UseWarp=true` opt-in while adding the module check
   needed for a WARP-compatible drop-in.
4. Record unavailable infrastructure as blocked, not Pass, per `W0(d)`'s own
   acceptance text.

This design does not invent a separate FeMe adapter identity or claim that
the current routing probe already loads a working device. The
[strict W0(g) audit](W0WindowsIntegrationProbe.md#w0g-strict-negotiation-gates-and-versioned-callback-audit-vm)
now checks the WARP adapter identity, requires the selected `d3d10warp.dll`
module handle to match the explicitly loaded owned shim, and rejects reference
forwarding. That is prototype-selection evidence only. The full runner
assertion still needs `W0(a)`/`W0(g)`'s real DDI/device path and a stable
FeMe build/package provenance value paired with that device.
