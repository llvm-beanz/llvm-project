# W0 Windows user-mode integration probe

**W0(b) decision: WARP-compatible application-local UMD drop-in.**
Stock D3D12 reaches LLVM-owned `OpenAdapter12`, queries its public adapter
callbacks, and closed it in the historical host and Windows-container
measurements. The owned
cases need neither ordinal 199 nor reference-WARP forwarding. This uses
Microsoft's existing WARP adapter identity, not a distinct FeMe adapter;
the user selected that identity and the unmodified WARP kernel-mode driver
for FeMe's application-local compatibility path on 2026-11-17. This closes
the product-choice gate only: no supported DDI/device/resource contract or
universal transparent substitution is proven. See
[Alternative B: application-local WARP DDI routing](#alternative-b-application-local-warp-ddi-routing).
The public general-provider search remains negative, but that is not a
runtime-routing impossibility result. No system WARP binaries or kernel-mode
driver are to be replaced or modified.
The [VM paging-queue follow-up](#w0g-public-kernel-paging-queue-lifecycle-vm)
now demonstrates a public kernel-service callback create/destroy round trip;
it still creates no working D3D12 device or buffer/texture allocation.

This report preserves historical host/container evidence for
[WARPRoadmap.md](WARPRoadmap.md), collected on 2026-11-06. Development and
validation now continue in a full-featured Windows VM, per the
2026-11-17 workflow change. The user selected stock-runtime
user-mode/software-adapter probing first, with no system changes. The LLVM-owned
[standalone probe](../utils/windows-adapter-probe/CMakeLists.txt) uses public
SDK/WDK types and APIs. It does not contain Microsoft implementation code,
invoke private WARP APIs, install an adapter, or execute FeMe shaders.

## W2(a) declared-binding preflight (VM, 2026-11-21)

Opt-in `--root-signature-constants` now checks live bound-resource
declarations before normalization or selection metadata attachment. Each
declaration must match the checked root plan's SRV/UAV/CBV/sampler class,
register space and stage visibility. One root binding must contain its
entire register range; neither adjacent ranges nor individual static
samplers collectively satisfy an array. Finite endpoints use wide
arithmetic; unbounded declarations extend to the end of the register domain.

The selected entry and reachable direct helper definitions are checked;
recursion terminates, unrelated functions and dead handles are ignored.
Dynamic/malformed declarations and indirect calls fail explicitly, without
attaching selection metadata. Dynamic-heap access is outside this static
check. The inferred default and artifact ABI remain unchanged.

Seven new unit cases cover resource classes, space/visibility, holes,
adjacent and unbounded ranges, individual samplers, canonical handle types,
helper reachability, malformed/dynamic declarations and indirect calls.
The real DXC fixture checks a two-element UAV array with three threads at
widths 4/8, scalar-reference and native AOT, retaining three dispatches and
verifying both complete output buffers and untouched guard values.
Wrong-class, wrong-space, wrong-visibility, gapped and adjacent layouts are
compiled with `/Vd` and rejected by FeMe. Native SDK validation independently
rejects the adjacent-range layout:

```text
Shader UAV descriptor range (RegisterSpace=7, NumDescriptors=2, BaseShaderRegister=3) is not fully bound in root signature
```

All automated tests run through LIT. Focused root/CPU/DXIL coverage passes
**827**, with zero failures, in **8.04 seconds**. After rebuilding all
prerequisites with assertions and disabled PCH, the full suite with native
SDK DXC/FXC passes **4,052**, with **15 Skipped, 10 Unsupported and zero
failures**, from **4,077** discovered tests in **136.86 seconds**. Eight
new passes correspond to the seven unit cases and one source fixture.
These are measured timings, not a controlled performance comparison.
No new Windows adapter-matrix or external Vulkan CTS result is claimed.

This is declared binding preflight, not table materialization, descriptor
contents, class-aware heap remapping, full resource-kind/counter/promise
compatibility or general CBV execution. Multiple inline parameters and
owned D3D12 device execution remain open; `CreateDevice` still returns
`E_NOTIMPL`. W0(c), W0(g), W1 and W2(a) are not closed.

## W2(a) explicit root-constant selection (VM, 2026-11-21)

`feme` and `feme-run` now expose opt-in `--root-signature-constants`,
backed by matching stage/JIT compilation options. It uses the checked
Root Signature 1.0/1.1 plan to select exactly one visible inline-constant
parameter, rather than treating a cbuffer's uses as its binding contract.
Missing/malformed signatures, multiple visible constant parameters,
unsupported stages, stage-denial flags and local signatures fail explicitly.
Compute accepts only all-stage parameters; conventional graphics stages
also accept their own visibility.

The selected register/space and full DWORD span control root reflection.
Each scalar is clipped against both its shader declaration and that
parameter, even if extra bytes are supplied. Preceding descriptor parameters
do not shift the inline byte block. Cbuffer arrays and other CBV bindings
remain unsupported, and descriptor-only layouts cannot silently become
inline constants. The default inferred mode is retained and tested.
Normal, scalar-reference, mixed heap/root and native AOT compilation share
selection metadata and guarded lowering; no artifact ABI revision is needed.

The first real DXC run exposed a frontend gap: RTS0 lived in the original
container but was lost before CPU selection. The shared DXIL/DXBC frontend
now copies those bytes onto canonical shader entries after metadata raising,
using the same container extraction as inspection. Per-entry signatures
take precedence; missing RTS0 is distinct from malformed/duplicate parts.

Nine new unit cases and three source fixtures cover:

- Exact parameter-required spans, including shader declarations smaller or
  larger than the inline parameter; malformed selection metadata is rejected.
- Visibility, denial flags, local/multiple parameters, wrong registers,
  array declarations, owned container bytes and per-entry precedence.
- A 16-byte IR cbuffer mapped to an 8-byte root parameter, with exact
  three-thread readbacks at widths 4/8, reference and AOT. Extra supplied
  values remain invisible; short AOT input is guarded. Inferred mode retains
  its original 16-byte behavior on the same shader.
- Real DXC 1.0/1.1 layouts with a UAV table before the constant parameter,
  overlapping b3/u3 register numbers in space 7, and native 16-/64-bit packing.
  Normal/reference/AOT output is checked across three retained dispatches.
- Original FXC SM5.1 DXBC with root constants, checked through normal
  widths 4/8, reference and AOT using a weighted four-DWORD checksum.
  Root-descriptor and table-backed CBV layouts fail in explicit mode;
  inferred-mode compatibility remains passing.
- Rejection of the compile option in object-dispatch/inspection modes and
  for non-CPU `feme` targets.

All automated tests run through LIT. Focused CPU/DXIL/frontend/driver/target
and source coverage passes **839**, with zero failures, in **2.57 seconds**.
After rebuilding all prerequisites and the native ARM64 ICD with assertions
and PCH disabled, the full suite with native SDK DXC/FXC passes **4,044**,
with **15 Skipped, 10 Unsupported and zero failures**, from **4,069**
discovered tests in **67.26 seconds**. This is a measured run time, not a
controlled performance comparison. No new Windows adapter-matrix or external
Vulkan CTS result is claimed; advertisements are unchanged.

This is a single-visible-parameter inline-constant subset of W2(a), not
general root argument materialization. Multiple inline parameters, general
CBVs, descriptor tables/preparation and full shader/root compatibility
remain open. Owned device execution remains absent and `CreateDevice`
still returns `E_NOTIMPL`; W0(c), W0(g), W1 and W2(a) are not closed.

## W2(a) wide constant rows and bounded reads (VM, 2026-11-21)

The shared CPU root-constant lowering now accepts all six canonical DXIL
scalar shapes: `i16`/`half`, `i32`/`float`, and `i64`/`double`, in the
128-bit `.8`/`.4`/`.2` rows respectively. It reuses the DXIL raiser's row
matcher, including rejection of empty, opaque, mixed and wrong-width
aggregates. The declared element-size times array-length span must fit
the ABI's `uint32_t` byte size; tests check the exact limit and standard
unsupported-operation errors above it.

Array and row offsets are widened before multiplication. Each component
requires a valid array index, a complete scalar within that array element,
and sufficient supplied bytes. Invalid large indices cannot wrap into a
valid address, and a row cannot borrow bytes from a following element.
This intentionally corrects the old whole-row behavior: valid components
of partial rows survive, while incomplete scalars read zero. Alignment is
bounded by the root block's four-byte guarantee and reduced for odd strides.

Four source fixtures and three unit cases exercise:

- All six scalar types in normal widths 4/8, scalar-reference and AOT
  execution, with three complete dispatch/readback snapshots per mode.
- Six-lane varying indices, partial waves, 12-byte array elements, invalid
  indices/rows that wrapped in the old 32-bit arithmetic, and guard words.
- Three-byte elements with `i16` loads requiring alignment 1; extra supplied
  bytes cannot complete a scalar outside its declared element.
- Real native ARM64 SDK DXC SM6.2 HLSL with `/enable-16bit-types`: `double`
  at offset 0, `uint64_t` at 8, `half` at 16 and `uint16_t` at 18. DXC reports
  a 20-byte cbuffer; the three-thread shader validates exact readback at both
  wave widths and in reference mode.
- AOT YAML root input, now packed through the same helper as normal/reference
  execution and retained across repeats. Short and empty AOT blocks bypass
  reflected span validation and verify the compiled load guards directly.

All automated execution is through LIT. Focused root/CPU/DXIL and existing
AOT coverage passes **700 cases, zero failures**, in **5.68 seconds**.
After rebuilding all FeMe prerequisites and the native ARM64 ICD with
assertions and PCH disabled, the complete suite with SDK FXC on PATH passes
**4,032**, with **15 Skipped, 10 Unsupported and zero failures**, from
**4,057** discovered tests in **184.88 seconds**. No artifact ABI or Vulkan
advertisements change. Windows adapter matrices were not rerun for this
portable compiler milestone; no external Vulkan CTS result is claimed.

This advances W2(a)'s non-32-bit and bounded-index prerequisite only.
It still promotes one use-inferred cbuffer binding, not an explicit root
layout or multiple descriptor-backed CBVs. Root descriptors, descriptor
tables and promise enforcement remain unwired to CPU dispatch. W0(c),
W0(g), W1 and W2(a) remain open; owned `CreateDevice` is still `E_NOTIMPL`.

## W2(a) root-signature parsing and immutable binding plans (VM, 2026-11-21)

[RootBindingPlan](../include/feme/Transforms/DXIL/RootBindingPlan.h), in
the existing portable `FeMeTransformsDXIL` library, now owns a read-only
Root Signature 1.0/1.1 layout. It reuses LLVM's DirectX container views and
binary-format types, not a Windows COM deserializer or a second runtime.
Raw RTS0 payloads, root-only containers and shader DXBC/DXIL containers
share the same parser. Missing/duplicate RTS0 parts, unsupported plan
versions and malformed input are explicit errors.

Plans retain root constants, CBV/SRV/UAV root descriptors, descriptor
tables, static sampler state, register class/space/range, shader visibility,
root flags and serialized descriptor/range flags. APPEND resolves relative
to the preceding range, including explicit gaps and offsets moving back
into unused table slots. Unbounded ranges are represented without
allocating an unbounded heap; APPEND after one is rejected, while a
nonoverlapping explicit following range is accepted. Tests cover:

- The exact 64-DWORD root budget and overflow, empty/invalid ranges,
  conflicting data flags and mixed resource/sampler tables.
- Register and table-slot overlap, distinct register classes at the same
  number, and disjoint stage visibility versus all-stage overlap.
- Static sampler collisions and preservation of filter/address/LOD state.
- Every truncated prefix of a complete layout, oversized counts/offsets,
  noncontiguous/backward range payloads and samplers before root payloads.
- Owned storage after the input bytes are overwritten, register lookup,
  and canonical `!feme.dxil.rootsignature` metadata: absence is distinct
  from malformed metadata or an invalid serialized layout.

The shared LLVM parser needed two directly coupled fixes: complete,
overflow-safe array/parameter bounds checks, and reading descriptor
ranges from their serialized absolute offset rather than assuming they
immediately follow the table header. Its old sampler fixtures were
truncated or declared an undersized 1.2 part; these were corrected instead
of weakening the checks. LLVM's existing 1.2 static-sampler parsing is
preserved, but FeMe binding plans explicitly accept only 1.0/1.1.

`feme-run --inspect-root-signature INPUT` prints the plan and returns
without loading a JIT or dispatching. Combining it with any dispatch
option is rejected. Native ARM64 SDK `10.0.26100.0` DXC and FXC compile
the LLVM-owned HLSL fixture. Real DXC shader containers cover 1.0 and 1.1,
and an extracted root-only container is also checked. FXC covers an
SM5.1 shader with its default 1.1 root signature and a separately compiled
`rootsig_1_0` container. Each reports six parameters, one static sampler
and exactly 12 DWORDs; the oracle checks all parameter/range rows,
spaces, APPEND offsets and serialized flags.

All automated execution is through LIT:

- Focused FeMe DXIL units, compiler fixtures and LLVM Object tests:
  **173 Passed, 2 Skipped, zero failures**.
- Rebuilt complete FeMe suite with SDK FXC on PATH:
  **4,025 Passed, 15 Skipped, 10 Unsupported, zero failures**, from
  4,050 discovered tests in **162.44 seconds**. This includes the twelve
  new plan unit cases and three new source fixtures.
- Rebuilt LLVM Object cases plus the pinned public-registry capability
  check: **154 Passed, 2 Skipped, zero failures**. Both Vulkan inventories
  reproduce unchanged against registry `f84d432d5b89`.

This does **not** replace use-inferred cbuffer promotion, prepare
descriptors, validate shader/root-layout compatibility, enforce 1.1
promises, implement DXR local arguments or establish owned D3D12
execution. The plans expose serialized flags for future frontend policy,
not a claim that every SDK semantic rule is enforced. Artifact ABI,
Vulkan advertisements and default CPU dispatch behavior are unchanged.
W2(a), W1(a)/W1(b) and owned `CreateDevice` (`E_NOTIMPL`) remain open.
The standalone Windows adapter matrices were not rerun for this
parsing-only milestone; their preceding results are historical evidence.

## Environment and limits

| Component | Measured configuration |
|---|---|
| Host | Windows 11 Enterprise 26H2, OS build 26300.9711, x64 |
| Compiler | VS 2022 Enterprise 17.14.44; MSVC 19.44.35229.0 |
| SDK/WDK headers and libraries | `10.0.28000.0`, including `d3d12umddi.h` |
| Manifest tool | SDK `10.0.26100.0` `mt.exe`; see the build issue below |
| Compiler cache / build | sccache 0.3.1, Ninja, Debug, assertions enabled, PCH disabled |
| Inbox `d3d12.dll` / `D3D12Core.dll` | `10.0.26100.9549` |
| Inbox `d3d10warp.dll` | `10.0.26100.9711` |
| Existing NuGet WARP | Package 1.0.19; file `1.0.19.0.20260422.1` |
| Existing Agility SDK | Package 1.619.5; core file `1.619.5.0.20260730.3`; exported SDK version 619 |
| Reference adapter | Microsoft Basic Render Driver, vendor `0x1414`, device `0x008c`, software flag; LUID `00000000:0001283b` in this boot |
| FeMe D3D12 DDI negotiation | Original generic probe: not reached. Alternative B below reaches owned adapter callbacks, but advertises no supported DDI version and supplies no device tables |
| FeMe Vulkan ICD | Not installed on this host; `vulkaninfo --summary` reports NVIDIA and Dozen adapters, not FeMe |

The probe prints the loaded D3D12 runtime/core/WARP module paths. Agility
selection was verified from the application-local core path, not merely from
the build options. LUIDs are boot-local evidence, not stable identity constants.
The reference test asserts Microsoft's vendor/device/software identity and
checks the opened device's LUID against the selected adapter.

No driver registration, signing changes, registry changes, system-DLL
replacement, or reboot occurred. No Vulkan implementation or capabilities
changed; Vulkan CTS and `check-feme` were not run. Their previous results are
not evidence for this Windows adapter probe.

## Measurements

The positive test opens a minimum-FL-11_0 D3D12 device, creates a compute queue
and fence, registers a completion event, signals value 1, checks completion
within 10 seconds, verifies the completed value, and releases the objects.
This is a focused public-API reference test, not shader execution or FL-12_2
qualification.

| Runtime / entry path | Result |
|---|---|
| Inbox core, `EnumWarpAdapter`, inbox WARP | Device, queue and fence test succeeds |
| Inbox core, `CreateSoftwareAdapter`, inbox WARP | Same test succeeds |
| Inbox core, `CreateSoftwareAdapter`, NuGet WARP 1.0.19 | Same test succeeds; requested WARP module is loaded |
| Agility 619 core, `EnumWarpAdapter`, inbox WARP | Same test succeeds; application-local Agility core is loaded |
| Agility 619 core, `CreateSoftwareAdapter`, inbox WARP | Same test succeeds |
| Agility 619 core, `CreateSoftwareAdapter`, NuGet WARP 1.0.19 | Same test succeeds |
| Inbox and Agility cores, `CreateSoftwareAdapter`, rejection stub | Adapter creation and `IDXGIAdapter1` query succeed; empty description, zero IDs/flags/LUID; `D3D12CreateDevice` returns `0x887a0004` (`DXGI_ERROR_UNSUPPORTED`) |
| Stub entry-point counters after rejection and adapter release | `OpenAdapter10=0`, `OpenAdapter10_2=0`, `OpenAdapter12=0`, on both cores |
| Inbox core, executable-local NuGet DLL named `d3d10warp.dll`, `EnumWarpAdapter` | Reference identity and device/queue/fence test succeed; local WARP path is reported |
| Inbox core, executable-local stub renamed `d3d10warp.dll`, `EnumWarpAdapter` | Microsoft reference identity is still enumerated, but device creation returns `DXGI_ERROR_UNSUPPORTED`; debugger observes the stub load, and it is no longer loaded when device creation returns |
| Nonexistent software DLL | Explicit load error `0x8007007e`, nonzero exit |

The probe has four cases: `reference-warp`, `software-reference-warp`,
`rejection-stub`, and `missing-module`. All four passed against each runtime.
The negative stub case requires successful adapter creation/interface query
followed by specifically `DXGI_ERROR_UNSUPPORTED`; an arbitrary earlier failure
is not accepted as a successful experiment.

**Test-runner correction (2026-11-17):** FeMe uses LIT, not CTest. The
standalone Windows probes now generate LIT shell tests and expose
`check-feme-windows-probes`; configure/build commands below use that target.
Historical CTest names/counts and historical command transcripts are retained
as evidence of what was actually run, not instructions to keep using CTest.
Select a Python interpreter with `psutil` installed (LIT requires it for the
30-second process-tree timeout), using `-DPython3_EXECUTABLE=<python-path>`
when configuring if necessary. `FEME_PROBE_LIT` defaults to this checkout's
`llvm/utils/lit/lit.py` and can select another existing LIT runner.

The stub deliberately returns unsupported from its exported DDI open functions.
Its counters distinguish rejection before those functions from rejection inside
them. It does not implement a complete software driver, adapter identity
bootstrap, or callback tables. Its failure therefore cannot prove that a valid
user-mode software adapter is impossible.

## What this resolves

1. **A new kernel driver is not established as mandatory.** Microsoft's public
   [CreateSoftwareAdapter contract](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createsoftwareadapter)
   permits emulating required kernel graphics components. Both tested D3D12
   runtimes actually open Microsoft's WARP through this API. That proves the
   reference path works, not a third-party hosting contract.
2. **The application-local WARP filename is honored in the tested configuration.**
   The working NuGet control and failing renamed stub rule out a blanket
   assertion that Windows always ignores executable-local WARP binaries.
   Filename substitution alone does not supply a compatible driver.
3. **The current generic stub fails before FeMe DDI negotiation.** Adapter
   creation success is not device creation success. A valid adapter/bootstrap
   contract is the immediate missing implementation, not the shader compiler.

The reference WARP export tables include a non-name export at ordinal 199;
the local reference definition labels it `WarpPrivateApi`. This is evidence of
a private compatibility surface, not proof that this export caused the observed
failure. Debugger lookup experiments did not establish that causal link.
No implementation, signature, or private call was copied or added to the
probe. The design's prohibition on depending on private WARP ordinals remains.

## Remaining work under the selected WARP-compatible contract

The user selected a WARP-compatible application-local UMD drop-in on
2026-11-17: keep Microsoft's WARP adapter identity and use the existing,
unmodified WARP kernel-mode driver. This closes the product-choice gate, not
the technical feasibility gate. The VM-first implementation must still
prove that a real FeMe device/resource lifecycle works through that existing
kernel-mode support. No system WARP binaries are replaced, no kernel driver is
installed or modified, and no application-local COM runtime is selected.

1. ~~Build this probe and run all four CTest cases inside the Windows
   container. Record OS/kit/runtime/core/WARP versions and loaded paths,
   identity/LUID, HRESULTs and DDI counters. Verify reference WARP first; if
   it fails, record and isolate the actual environment/runtime blocker rather
   than using the host results as a substitute or assuming a kernel driver is
   required.~~ Completed 2026-11-06; see
   [Container measurements](#container-measurements) below. All four cases
   passed, with an extended NuGet-WARP/Agility-core matrix also passing.
2. ~~Identify the public, modern third-party software-adapter bootstrap contract
   needed to provide valid identity/thunk services and reach `OpenAdapter12`.
   No kernel installation is needed to build or run the loading experiments.~~
   Completed 2026-11-06 with a negative result; see
   [Decision: no public third-party route to `OpenAdapter12` was found](#decision-no-public-third-party-route-to-openadapter12-was-found).
   No such contract was found; the later W0(b) choice selected the separately
   measured WARP-specific application-local route, not a general provider.
3. Extend an LLVM-owned prototype through open/callback negotiation/close under
   Microsoft's WARP identity, preserving the WARP device IDs and using the
   unmodified WARP kernel-mode driver. Measure the chosen DDI version and
   teardown, including failure cleanup, rather than inferring support from the
   installed headers.
   The [direct-call experiment](#direct-openadapter12-invocation-bypassing-dxgi)
   proves the entry point's mechanics; alternative B below now proves
   stock-runtime routing and adapter queries/close without a new kernel driver.
   Supported-version negotiation, real device creation, and FeMe-module
   provenance remain separate unfinished requirements; a distinct adapter
   identity is not part of the selected contract.
4. The selected scope does not authorize undocumented private WARP ABI, ordinal
   199, system-WARP replacement, or a new kernel component. If one proves
   necessary, stop and return to the user rather than silently expanding scope.
   The route must also demonstrate FeMe-module provenance independently from
   the intentionally shared WARP adapter identity.

W0(a) and W0(d) remain incomplete: there is no FeMe D3D12 device, no verified
resource lifecycle through WARP's existing kernel-mode driver, and no runner
that asserts both WARP identity and FeMe-module provenance. W0(b)'s selected
product boundary is closed. The user-mode stock-runtime path was historically
verified in the container through owned adapter queries/close in alternative
B, but that is not a real-device result. No public
general-provider bootstrap was found; the
WARP-specific route is separately proven and remains subject to its
application-local compatibility limits. Installed-driver, signing/reboot,
display and HLK work are outside the selected deployment contract. Missing
evidence is not Pass.

## Container measurements

Historical results collected 2026-11-06 in the process-isolated Windows
container; these are retained as evidence, not as the current development
environment. Current implementation and validation proceed in the Windows VM.
The measurements used the
same LLVM-owned standalone probe as the host measurements above (no changes
to `AdapterProbe.cpp`/`RejectionStub.cpp` were needed to build or run here).

| Component | Measured configuration |
|---|---|
| Container OS | Windows Server 2025 Datacenter 24H2, OS build 26100.33650, x64 |
| Compiler | MSVC 19.51.36260.0 (`C:\BuildTools` VS 2026 Build Tools) |
| SDK/WDK kit selection | `10.0.28000.0` (`WINDOWS_KIT_VERSION`); no `CMAKE_MT` workaround needed -- the container's `mt.exe` launches cleanly, unlike the host's SDK 28000 `mt.exe` failure |
| Compiler cache / build | sccache 0.18.0, Ninja, Debug, assertions enabled (container default), PCH disabled |
| Inbox `d3d12.dll` / `D3D12Core.dll` / `d3d10warp.dll` | all `10.0.26100.33648` |
| Container NuGet WARP (via `DirectXShaderCompiler/build-rel`) | package `1.0.21`; file `1.0.21.0.20260922.2` |
| Container NuGet WARP (via `llvm-project/build/bin`, from the OffloadTest D3D12 test-infrastructure restore) | package `1.0.19`; file `1.0.19.0.20260422.1` |
| Container Agility SDK core (via `llvm-project/build/tools/OffloadTest/agility-sdk`) | file `1.619.5.0.20260730.3`; exported SDK version 619, matching the host's package |
| Reference adapter | Microsoft Basic Render Driver, vendor `0x1414`, device `0x008c`, software flag; LUID `00000000:0001283b` in this container boot |
| FeMe D3D12 DDI negotiation | Original generic probe: not reached. Alternative B below reaches owned adapter callbacks in this container; no supported DDI version/device tables |

Configured and built with the exact commands from the "Container starting
point" section below; all four CTest cases passed, with identical pass/fail
shape to the host measurements:

| Runtime / entry path | Container result |
|---|---|
| Inbox core, `EnumWarpAdapter`, inbox WARP | Device, queue and fence test succeeds; `d3d12.dll`/`D3D12Core.dll`/`d3d10warp.dll` all load from `System32` |
| Inbox core, `CreateSoftwareAdapter`, inbox WARP | Same test succeeds |
| Inbox core, `CreateSoftwareAdapter`, container NuGet WARP 1.0.19 (`llvm-project\build\bin\d3d10warp.dll`) | Same test succeeds; requested WARP module is loaded from the supplied path |
| Inbox core, `CreateSoftwareAdapter`, container NuGet WARP 1.0.21 (`DirectXShaderCompiler\build-rel\...\bin\x64\d3d10warp.dll`) | Same test succeeds |
| Agility 619 core (rebuilt against `llvm-project\build\tools\OffloadTest\agility-sdk\...\D3D12Core.dll`), `EnumWarpAdapter`, inbox WARP | Same test succeeds; application-local Agility core is loaded from `AgilitySDK\D3D12Core.dll` |
| Agility 619 core, `CreateSoftwareAdapter`, inbox WARP | Same test succeeds |
| Agility 619 core, `CreateSoftwareAdapter`, container NuGet WARP 1.0.19 | Same test succeeds |
| Agility 619 core, `CreateSoftwareAdapter`, rejection stub | Adapter creation and `IDXGIAdapter1` query succeed; empty description, zero IDs/flags/LUID; `D3D12CreateDevice` returns `0x887a0004` (`DXGI_ERROR_UNSUPPORTED`); stub counters `OpenAdapter10=0 OpenAdapter10_2=0 OpenAdapter12=0` |
| Inbox core, `CreateSoftwareAdapter`, rejection stub (CTest `rejection-stub`) | Identical to the Agility-core row above |
| Inbox core, missing software module (CTest `missing-module`) | `LoadLibraryExW` fails with `0x8007007e`, nonzero exit, as expected |

This closes the measurement half of `W0(f)` with no discrepancy from the host
results: the container's own inbox D3D12/WARP stack, and every container-local
NuGet WARP/Agility SDK package already present from unrelated FeMe/DXC/
OffloadTest build restores, all pass the same reference, software-adapter,
and rejection-stub experiments as the host did. No driver registration,
signing changes, registry changes, system-DLL replacement, or reboot
occurred in the container either. In these generic `CreateSoftwareAdapter`
runs the rejection stub's `OpenAdapter12` counter stays at zero. This
does not generalize to every loader path: alternative B below routes even
the original rejection stub through `EnumWarpAdapter` to its owned DDI.

## Direct `OpenAdapter12` invocation (bypassing DXGI)

Collected 2026-11-06 in the container, using a second LLVM-owned standalone
project, [windows-adapter-bootstrap](../utils/windows-adapter-bootstrap/),
kept separate from `windows-adapter-probe` above. It answers a question the
rejection-stub result above could not: is `OpenAdapter12` and the DDI table
it fills in mechanically callable at all, independent of whether DXGI ever
routes to it?

`BootstrapStub.cpp` implements a real (if minimal) `OpenAdapter12` that fills
in a public `D3D12DDI_ADAPTERFUNCS` table with instrumented functions, each
returning a deliberate, distinguishable result (`E_NOTIMPL` past the open/
negotiate boundary; no real device, resource, or command contract).
`DirectHarness.cpp` does not use DXGI or `D3D12CreateDevice` at all: it loads
the stub directly with `LoadLibraryExW`, resolves `OpenAdapter12` with
`GetProcAddress`, and calls it by hand with a zeroed `D3DDDI_ADAPTERCALLBACKS`
(safe only because this stub's DDI functions never invoke it) and a minimal
`D3D12DDIARG_OPENADAPTER`.

| Call (direct, not OS-routed) | Result |
|---|---|
| `OpenAdapter12` | `S_OK`; `D3D12DDI_ADAPTERFUNCS` fully filled; `hAdapter` set |
| `pfnGetSupportedVersions` | `S_OK`; 0 supported interface versions |
| `pfnGetCaps` | `E_NOTIMPL` (deliberate) |
| `pfnCalcPrivateDeviceSize` | `64` (deliberate sentinel) |
| `pfnGetOptionalDDITables` | `S_OK`; 0 optional tables |
| `pfnCreateDevice` | `E_NOTIMPL` (deliberate) |
| `pfnCloseAdapter` | `S_OK` |
| Call counters after the sequence | each of `OpenAdapter12`/`CalcPrivateDeviceSize`/`GetSupportedVersions`/`GetCaps`/`GetOptionalDDITables`/`CreateDevice`/`CloseAdapter` is exactly 1; `FillDDITable`/`DestroyDevice` are 0 (never exercised by this sequence) |

CTest case `direct-openadapter12` passed. **This is a direct DDI call, not an
OS-routed adapter; it is not evidence that DXGI/D3D12CreateDevice can reach
this module.** It does establish that the public `OpenAdapter12`/
`D3D12DDI_ADAPTERFUNCS` contract, as declared in the WDK's `d3d12umddi.h`, is
self-consistently implementable and callable in user mode with no kernel
involvement in this direct-call fixture. At that point OS-side routing was
unproved; alternative B below subsequently proves routing to the same fixture.

### Decision: no public third-party route to `OpenAdapter12` was found

Combining this session's direct-call result with the prior rejection-stub
result and public documentation research:

1. The rejection stub's `OpenAdapter10`/`OpenAdapter10_2`/`OpenAdapter12`
   counters all stay at zero through a full `CreateSoftwareAdapter` ->
   `D3D12CreateDevice` -> `DXGI_ERROR_UNSUPPORTED` sequence (host and
   container, this and the prior session). `D3D12CreateDevice` rejects the
   software-adapter object before touching any UMD entry point.
2. Microsoft's public
   [`IDXGIFactory::CreateSoftwareAdapter`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-createsoftwareadapter)
   documentation only references "the Windows Vista Driver Development Kit"
   (the D3D10-era UMD contract) and does not document an `OpenAdapter12`/
   D3D12 contract. No other public DXGI/D3D12/DXCore API documents a
   third-party route to `OpenAdapter12`; DXCore's adapter enumeration is
   explicitly documented as listing only adapters a kernel-mode (WDDM/MCDM)
   driver has already registered with Windows, not a way to register a
   purely user-mode adapter.
3. This session's direct-call experiment above shows the entry point and
   table-filling mechanics are not themselves the obstacle -- a correctly
   shaped `OpenAdapter12` is trivially callable by hand. The obstacle is
   specifically the absence of a documented general-provider bootstrap,
   not evidence that stock Windows can never load an owned `OpenAdapter12`.
4. The reference WARP export table's private ordinal-199 `WarpPrivateApi`
   (noted in the host measurements above) is an observed compatibility surface,
   not proof it is required for loading an owned DDI. Alternative B below
   reaches owned adapter callbacks without this export.

**No public, third-party, no-install bootstrap contract to `OpenAdapter12`
was found.** This answers `W0(g)`'s "identify a public third-party adapter
identity/thunk bootstrap" step with a negative result rather than a positive
one. This originally triggered `W0(b)`'s decision gate among (a) an installed
adapter, (b) WARP-specific compatibility, or (c) an application-local COM
runtime. The user selected B for the application-local product boundary on
2026-11-17. Its successful routing result below supports that choice but does
not prove a working device or a WARP-kernel resource contract.

### `W0(b)` decision: WARP-compatible application-local UMD

Collected 2026-11-07, from Microsoft's public driver-signing documentation
and the WARP architecture documented by Microsoft's own public articles. This
section records the user's selection on 2026-11-17 and the boundaries that
follow from it. The comparison below remains useful context; it is no longer
an open decision gate.

The selected product is an application-local FeMe user-mode driver intended
to act as a WARP drop-in under Microsoft's WARP adapter identity
(`VendorId=0x1414`, `DeviceId=0x008c`, software-adapter flag), relying on the
existing, unmodified WARP kernel-mode driver. FeMe does not install a new
miniport, modify that kernel driver, replace system WARP binaries, or provide
its own D3D12/DXGI COM runtime. The choice is based on WARP-specific
compatibility, not a public third-party provider-registration contract.

"Drop-in" is bounded to the application-local loading/selection path under
investigation; it does not claim that every application or Windows runtime
will transparently load FeMe for `D3D_DRIVER_TYPE_WARP` or
`EnumWarpAdapter`. The probe establishes owned UMD routing and callback
negotiation under that identity, but has no real device and has not invoked
the kernel-routed allocation/residency callbacks. A real device/resource
lifecycle using WARP's existing kernel-mode support is therefore a required
W0(g) acceptance gate. If that path requires an undocumented private ABI,
system DLL replacement, or a new kernel component, stop and return to the
user rather than silently changing the selected scope.

**(a) Installed-adapter (kernel-mode driver), signing cost.** Microsoft's
[Driver Signing offerings](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/driver-signing-offerings)
page documents three signing tiers and a per-OS acceptance table:

- *Attestation signing* ("testing scenarios"): no HLK test required, only an
  EV code-signing certificate and a dashboard submission, but the same page's
  acceptance table states attestation-signed drivers are **not accepted on
  Windows Server >= 2016** (the row reads "No"; only Windows 10/11 Desktop and
  Windows IoT Enterprise accept it). The page's "Windows Server signed
  drivers" section makes this explicit: *"Windows Server 2016 and greater
  doesn't accept attested device and filter driver signing submissions...
  Windows Server 2016 and greater only loads dashboard signed drivers that
  successfully pass the HLK tests."* Since this container's measured OS is
  Windows Server 2025 Datacenter 24H2 (see the Environment and limits table
  above), attestation signing is **not available for this container's own OS
  family** regardless of host/VM choice, though it would remain available on
  a Windows 11 desktop host.
- *HLK-tested, dashboard-signed*: works on Windows Vista and later including
  all Windows Server editions, but requires passing Microsoft's full Hardware
  Lab Kit test suite (reliability, security, power efficiency, serviceability,
  performance, industry-standard compliance) and a Partner Center dashboard
  submission -- a substantial, open-ended engineering and process investment
  disproportionate to a research/compiler project, and the only signing tier
  Windows Server will load outside test-signing mode.
- *Preproduction signing*: a third tier for partners validating early driver
  builds with Secure Boot still enabled; requires explicitly provisioning
  target devices to trust the preproduction signature via Hardware Dev Center
  APIs (see
  [Manage preproduction signing submissions](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/manage-preprod-submissions)).
  This still requires a partner-level Hardware Dev Center relationship and
  per-device provisioning; it is not a zero-setup local option.
- *Test signing* (`bcdedit /set testsigning on`, per Microsoft's
  [Loading Test Signed Code](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)
  page): the lightest-weight option, loads self-signed/test-certificate
  drivers with no Partner Center submission, but secondary sources
  consistently report that enabling it on a Secure-Boot-enabled system fails
  (*"The value is protected by Secure Boot policy and cannot be modified or
  deleted"*) until Secure Boot is disabled in firmware, after which Windows
  shows a persistent "Test Mode" desktop watermark. This requires a dedicated
  development host or VM with firmware-level access to toggle Secure Boot and
  a reboot -- **not possible from inside this process-isolated container**,
  consistent with prior sessions' conclusion that kernel-driver work needs a
  separate host/VM. Realistically, alternative (a) for a research project
  like FeMe means a dedicated test-signed development VM, not an HLK-certified
  production driver; the HLK path's cost should only be paid if a signed,
  shippable driver is later a hard requirement.
- Unverified secondary-source claims (an EV certificate requiring a hardware
  token, a specific "April 2026" cross-signing trust removal date, and a
  "March 2027" SBOM/VEX documentation requirement for Windows Server driver
  submissions) could not be confirmed against the primary Microsoft pages
  fetched this session and are recorded here only as items needing
  verification before being relied upon, not as established fact.

**(b) WARP-specific compatibility, selected.** Application-local WARP
filename/identity selection is the provider boundary. The bounded
investigation uses the public `EnumWarpAdapter` API and public WDK DDI
signatures to enter owned adapter callbacks; it does not hook APIs or guess
ordinal-199 signatures. This is not a documented general third-party
registration contract, and the Microsoft adapter identity is intentionally
retained rather than presented as a distinct FeMe identity. Any later
private-API requirement adds compatibility and licensing-review costs;
ordinals can change without a compatibility guarantee. The selected scope
does not authorize such a dependency.

**(c) Application-local COM runtime, cost.** This means FeMe owning its own
D3D12/DXGI-shaped API surface entirely outside the real Microsoft runtime
(own `IUnknown`/COM activation, own adapter/factory/device interface
implementations binary-compatible with `d3d12.h`/`dxgi.h` signatures, so
existing D3D12 application code can link/call it unmodified), rather than
being reached through the real `d3d12.dll`/`dxgi.dll`/DXGI adapter
enumeration at all. Concretely this adds: (1) COM activation/lifetime
plumbing (`IUnknown::QueryInterface`/`AddRef`/`Release`, interface GUID
tables) for every public interface FeMe exposes; (2) a DXGI-equivalent
factory/adapter/swap-chain surface, since real DXGI enumeration would not
list this adapter at all; (3) debug-layer and DRED (Device Removed Extended
Data) equivalents, both already real D3D12 application-facing features that
existing test content may depend on; (4) a distribution/injection story,
since applications must be redirected to the FeMe-local runtime instead of
the system `d3d12.dll`/`dxgi.dll` (e.g. via an application-local DLL next to
the executable, as many non-redistributable game-engine or test-harness
DLLs already do) rather than any system-wide registration. Unlike (a) and (b),
this alternative needs no OS-level signing or private-API dependency and
remains fully buildable/testable inside this container, at the cost of
reimplementing a second, parallel public-API surface rather than plugging
into the real one.

## Alternative B: application-local WARP DDI routing

Collected 2026-11-06 after the user explicitly requested investigation of B.
The original approval covered bounded, application-local diagnostics, not
installation, system hooks, or guessed private ABIs. The later W0(b) decision
selects this WARP-specific compatibility boundary as the product direction;
it does not convert the probe into a working driver or authorize private ABI.
The new LLVM-owned
[windows-warp-interposition](../utils/windows-warp-interposition/CMakeLists.txt)
project reuses the probe harness and public adapter-table fixture. No
Microsoft source/test implementation was copied.

### Controlled export matrix

Each case has its own executable directory and DLL named `d3d10warp.dll`.
The forwarding controls copy only the explicitly supplied reference DLL into
their build directories as `feme_warp_reference.dll`. CMake verifies SHA256
`1fd2f9d991f00d890d06c00f791a756446f5f29da508b12fdeb8ffd1b5696add`
(the existing x64 NuGet WARP 1.0.19 binary) before copying it.

| Variant / CTest case | Export policy | Measured result |
|---|---|---|
| `reference` / `reference-forwarding` | Known named exports, `OpenAdapter12`, and ordinal 199 forwarded to reference WARP | Microsoft device/queue/fence control succeeds; not FeMe coverage |
| `public-exports` / `public-exports-interception` | Other named exports forwarded; owned rejecting `OpenAdapter12`; no ordinal 199 | Owned `OpenAdapter12=1`; unsupported by design |
| `private-ordinal` / `private-ordinal-interception` | Same owned open plus ordinal-199 forwarder | Same owned `OpenAdapter12=1`; ordinal forwarding is unnecessary for this stage |
| `bare` / `bare-interception` | Only owned open/counter exports; no forwarding or reference DLL | Owned `OpenAdapter12=1`; unsupported by design; reference DLL not loaded |
| `negotiation` / `os-routed-negotiation` | Owned `OpenAdapter12` fills the public adapter table; no forwarding/reference DLL | Windows queries supported versions/caps and closes the adapter; no device |
| `pinned-version` / `os-routed-pinned-version` | Same as `negotiation`, but pins one public `D3D12DDI_SUPPORTED_0010` version and logs `GetCaps` types/`CreateDevice` thunk-pointer presence | Identical call counters to `negotiation`; `CreateDevice` still unreached -- see [W0(g) pinned-DDI/thunk-service audit](#w0g-pinned-ddithunk-service-audit) |
| `bare` / `generic-software-rejection` | Same DLL, but selected through `CreateSoftwareAdapter` instead of `EnumWarpAdapter` | Zero adapter identity and `OpenAdapter12=0`; unsupported before owned DDI |

The owned WARP cases retain an explicit absolute-path `LoadLibraryExW`
reference, call `EnumWarpAdapter`, and pass that adapter to the real
`D3D12CreateDevice`. Microsoft Basic Render Driver remains enumerated:
vendor `1414`, device `008c`, software flag, LUID `00000000:0001283b`
in this boot. The harness asserts that identity as a diagnostic limitation,
not a FeMe-selection check. D3D12 ultimately returns `0x887a0004` deliberately:
the bare entry rejects opening; the table fixture advertises zero versions
and rejects capabilities. These are routing tests, not working devices.

Exact table-fixture counters on host inbox, host Agility 619, container inbox
and container Agility 619:

```text
Open=1 Size=0 Versions=2 Caps=2 Optional=0 Fill=0 Create=0 Close=1 Destroy=0
```

The test requires one open/close, both kinds of query, and zero device/table
creation callbacks. It does not assume a universal exact query count across
future runtimes. The independent owned cases have no PE forwarders and never
load `feme_warp_reference.dll`; the negotiation harness rejects such a dependency.

### Loader contrast and corrected interpretation

A debugger breakpoint in the bare DLL conclusively records stock
`D3D12Core!NDXGI::CUMDAdapter::OpenAdapter<0>` calling the owned entry point.
The same entry is also reached **without explicit preloading**, when the DLL
is executable-local and named `d3d10warp.dll`. The runtime calls
`LoadLibraryExW("d3d10warp.dll", ..., 0x8)` on the measured host
(`LOAD_WITH_ALTERED_SEARCH_PATH`). The DLL is released after its deliberate
failure, so a module report after `D3D12CreateDevice` says "not loaded".
That report is not evidence that its entry point was never called. Explicit
preloading is useful here to retain counters through failure cleanup, not a
proven routing prerequisite.

The original rejection stub, renamed locally and retained by `--warp-shim`,
also records `OpenAdapter12=1`. Conversely, the same bare DLL through
`--stub`/`CreateSoftwareAdapter` records zero open calls. The original generic
negative result is valid; extending it to WARP-specific selection was not.
Ordinal 199 appears in reference-runtime lookup traces, but neither its
presence nor that lookup establishes a requirement for this owned bootstrap.

### Verification and remaining boundary

Both projects were built with x64 MSVC, SDK/WDK 28000, Debug `/Z7`, sccache,
assertions enabled and PCH disabled. The interposition project explicitly
selects CMake's `Embedded` debug-information format so `/Zi` does not override
`/Z7`. Container cases were built from staged LLVM-owned sources with the
container's compiler and inbox binaries, not just host-built DLLs:
Windows Server 2025 build `26100.33650`; `dxgi.dll`/`d3d12.dll`/
`D3D12Core.dll`/inbox WARP `10.0.26100.33648`;
`dxcore.dll` `10.0.26100.32995`. Host pins are listed above.
Both Agility runs loaded local core `1.619.5.0.20260730.3`, SDK version 619.

| Repeated validation | Result |
|---|---|
| Six interposition CTests, host inbox, each repeated 3 times | 18/18 |
| Six interposition CTests, container inbox, each repeated 3 times | 18/18 |
| Original four probe CTests, host inbox and host Agility, each repeated 3 times | 24/24 |
| Original four probe CTests, source-built container Agility, each repeated 3 times | 12/12 |
| Bare/negotiation/generic-negative controls, host and container Agility, each repeated 3 times | 18/18 |

**Conclusion:** no new/custom kernel driver, installation, reference forwarding,
or ordinal 199 is needed to route stock Windows into owned adapter callbacks
on these measured runtimes. This uses the existing OS WARP adapter
infrastructure; it does not prove that the kernel-routed resource callbacks
work for FeMe. Microsoft's WARP identity is intentionally selected, but no
supported DDI version, actual FeMe D3D12 device, feature level, shader
execution, presentation, or working drop-in implementation is established.
Private dependencies later in device creation remain unknown.

The next bounded technical step is to audit a pinned public DDI version and
the adapter/device/thunk services it requires, separating missing owned
implementation from any genuinely required private boundary. Seek approval
before expanding into private ABI implementation or changing the product's
identity/distribution contract. Do not return to driver installation merely
because the public general-provider search was negative.

### W0(g) pinned-DDI/thunk-service audit

Collected 2026-11-06, extending `negotiation` with a seventh `pinned-version`
variant and `os-routed-pinned-version` CTest. `BootstrapStub.cpp`'s
`getSupportedVersions` still advertises zero versions by default; the
`pinned-version` build instead defines `FEME_BOOTSTRAP_PINNED_VERSION` to
`D3D12DDI_SUPPORTED_0010` (Interface Version R1, Build 10 -- the earliest
public D3D12 UMD DDI baseline in `d3d12umddi.h`), the lowest-surface value to
audit first. `getCaps` now also logs the public `D3D12DDICAPS_TYPE` value of
every query (`FeMeBootstrapGetAudit`/`FeMeBootstrapGetAuditCapacity`), and
`createDevice` records whether its publicly documented thunk-service pointers
(`pKTCallbacks`: kernel-routed `D3DDDI_DEVICECALLBACKS`;
`p12UMCallbacks`: user-mode-only `D3D12DDI_CORELAYER_DEVICECALLBACKS_0003`)
are non-null, without implementing any capability or device contract.

Measured result, container inbox runtime, repeated 3 times
(`ctest --repeat until-fail:3`; 7/7 passed per run across all seven
interposition CTests, including the new `os-routed-pinned-version`):

```text
Bootstrap calls: Open=1 Size=0 Versions=2 Caps=2 Optional=0 Fill=0 Create=0 Close=1 Destroy=0
GetCaps Type values queried (2 total): 1074 1007
CreateDevice args: Interface=0 Version=0 pKTCallbacks=null p12UMCallbacks=null
```

This is identical to the zero-advertised-version baseline's counters: pinning
one supported interface version does not by itself change how many times
`GetSupportedVersions`/`GetCaps` are called, and `CreateDevice` is still never
reached (`Create=0`). The two logged `D3D12DDICAPS_TYPE` values are
`D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1` (1074) and
`D3D12DDICAPS_TYPE_3DPIPELINESUPPORT` (1007): the runtime queries the newer,
input/output `D3D12DDI_3DPIPELINESUPPORT1_DATA_0081` cap first (it carries the
runtime's own requested `HighestRuntimeSupportedFeatureLevel`, matching the
probe's `D3D12CreateDevice(..., D3D_FEATURE_LEVEL_11_0, ...)` call), then
falls back to the older enum-only `D3D12DDICAPS_TYPE_3DPIPELINESUPPORT` when
the first attempt fails. Both are answered with `E_NOTIMPL` here, and no
later cap (shader model, options, architecture info, ...) is ever reached.

**This sharpens, not resolves, the open boundary.** Both caps types, their
exact structs, and `D3D12DDIARG_CREATEDEVICE_0003`'s thunk-service pointer
types are fully public, documented in `d3d12umddi.h` -- no private ordinal,
undocumented struct, or guessed signature is involved. The reason
`CreateDevice` is never reached is that this experiment deliberately never
answers a capability query with success; that is **missing owned
implementation, not a demonstrated private boundary**. The genuinely private
question -- whether an owned user-mode driver can actually be bound to a
kernel-mode WDDM adapter object without installing one, i.e. how an
existing-identity UMD like WARP's satisfies `pKTCallbacks`'s kernel-routed
thunks -- remains unanswered, because negotiation never reaches the point
where those pointers would be populated (`pKTCallbacks`/`p12UMCallbacks` are
both still logged `null`, since `createDevice` itself is never invoked).

The next bounded step is to answer `D3D12DDICAPS_TYPE_3DPIPELINESUPPORT` and
`_0081_3DPIPELINESUPPORT1` truthfully and minimally (e.g. echoing back the
runtime's own requested feature level rather than fabricating a larger one),
while continuing to fail every other cap and still returning `E_NOTIMPL` from
`createDevice`, to determine whether capability negotiation alone is
sufficient to reach `CreateDevice` with this pinned version, or whether the
version itself (`D3D12DDI_SUPPORTED_0010`) is rejected independently of caps.

### W0(g) follow-up: `CreateDevice` is reached, both thunk-service pointer
### families are non-null

Collected 2026-11-06 (same day, follow-up measurement). `getCaps` in
`BootstrapStub.cpp` now answers the two previously-logged caps truthfully and
minimally, still without fabricating any other capability:

- `D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1`: echoes the runtime's own
  `HighestRuntimeSupportedFeatureLevel` input field back as
  `MaximumDriverSupportedFeatureLevel` -- a true statement, not a larger
  fabricated claim, since the data the UMD "discovers" is exactly what the
  runtime already told it.
- `D3D12DDICAPS_TYPE_3DPIPELINESUPPORT`: reports
  `D3D12DDI_3DPIPELINELEVEL_1_0_GENERIC`, the lowest defined level -- a
  truthful floor, not a fabricated higher tier.

Every other cap, and `createDevice` itself, are still left returning
`E_NOTIMPL`/deliberately failing exactly as before; only the two already-
observed cap queries gained a real (if minimal) answer.

Measured result, container inbox runtime, repeated 3 times
(`ctest --repeat until-fail:3`; 7/7 passed per run):

```text
Bootstrap calls: Open=1 Size=0 Versions=2 Caps=1 Optional=0 Fill=0 Create=1 Close=1 Destroy=0
GetCaps Type values queried (1 total): 1074
CreateDevice args: Interface=786442 Version=0 pKTCallbacks=non-null p12UMCallbacks=non-null
```

Three findings here:

1. **`CreateDevice` is reached (`Create=1`).** Answering the first cap
   (`D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1`, 1074) truthfully was
   sufficient; the runtime never falls back to the older
   `D3D12DDICAPS_TYPE_3DPIPELINESUPPORT` query at all once the newer one
   succeeds (`Caps=1`, not `2`), confirming the earlier hypothesis that 1007
   is a fallback for runtimes/drivers that don't answer 1074. This resolves
   the "missing owned implementation, not a demonstrated private boundary"
   framing from the prior measurement in the affirmative: it genuinely was
   just missing implementation. No private ordinal, undocumented struct, or
   guessed signature was needed to get this far.
2. **`Interface=786442` (`0x000C000A`) decodes exactly to the pinned
   version.** `D3D12DDI_MAJOR_VERSION` is `12` (`0x0C`), and
   `D3D12DDI_SUPPORTED_0010`'s `D3D12DDI_INTERFACE_VERSION_R1` component is
   `0x000A` (minor version `10`); `Version=0` matches
   `D3D12DDI_BUILD_VERSION_0010`'s value of `0`. The runtime negotiated
   precisely the one interface version this experiment advertised -- not a
   different or higher one it silently substituted.
3. **Both thunk-service pointer families are non-null.** `pKTCallbacks`
   (the kernel-routed `D3DDDI_DEVICECALLBACKS*`) and `p12UMCallbacks` (the
   user-mode-only `D3D12DDI_CORELAYER_DEVICECALLBACKS_0003*`) are both
   populated by the stock runtime when it calls `createDevice` on this
   LLVM-owned, non-installed `d3d10warp.dll`. This is the first direct
   evidence bearing on the "genuinely private question" from the prior
   measurement: the runtime supplies a real, usable kernel-routed callback
   table to an owned UMD identity reached purely through `EnumWarpAdapter`'s
   existing-identity substitution, with **no kernel-mode driver installed by
   this experiment**. This does not yet prove those kernel-routed callbacks
   can be *invoked* successfully (no resource/allocation call has been
   attempted against them) -- only that the pointer itself is real and
   non-null at this stage, routed through whatever KM adapter object already
   backs the system's WARP/Microsoft Basic Render Driver identity. The next
   bounded step is to dereference and call one read-only-feeling entry (e.g.
   a diagnostic/logging-style callback, not an allocation) to see whether it
   is actually live, before considering any real resource contract.

This measurement still implements no capability beyond the two caps above and
no device contract; `createDevice` still returns `E_NOTIMPL` after capturing
the audit evidence, so the probe's overall `FAILED(Result)` expectation is
unchanged and no new CTest assertion needed loosening.

### W0(g) follow-up: callback liveness probe and newer-version control

Collected 2026-11-07 (same container session, direct continuation of the
prior measurement). Two independent, bounded experiments, both still
implementing no capability beyond the existing two caps and still returning
`E_NOTIMPL` from `createDevice`:

**1. Callback-liveness probe (`callback-probe` variant, pins
`D3D12DDI_SUPPORTED_0010`, the same version that already reached
`CreateDevice`).** Rather than only null-checking `p12UMCallbacks`, this
variant also dereferences and *calls* exactly one entry --
`p12UMCallbacks->pfnSetErrorCb(hRTDevice, E_NOTIMPL)` -- wrapped in SEH
(`__try`/`__except`) so a fault would be caught and reported rather than
crashing the harness. `pfnSetErrorCb` was chosen deliberately because it is
pure user-mode (its first parameter is `D3D10DDI_HRTDEVICE`, with no
kernel-mode-routed argument) and purely diagnostic -- the documented
mechanism for a driver to report a device error back to the runtime, not an
allocation/resource operation. The two representative kernel-routed
(`pKTCallbacks`) entries, `pfnAllocateCb`/`pfnDeallocateCb`, are still only
null-checked, never invoked, per the explicit "not an allocation/resource
call" instruction.

Measured result, container inbox runtime, repeated 3 times
(`ctest --repeat until-fail:3`; 9/9 passed per run):

```text
Bootstrap calls: Open=1 Size=1 Versions=2 Caps=1 Optional=0 Fill=0 Create=1 Close=1 Destroy=0
GetCaps Type values queried (1 total): 1074
CreateDevice args: Interface=786442 Version=0 pKTCallbacks=non-null p12UMCallbacks=non-null
CreateDevice inner callback pointers (read-only, never invoked unless noted): pKTCallbacks->pfnAllocateCb=non-null pKTCallbacks->pfnDeallocateCb=non-null p12UMCallbacks->pfnSetErrorCb=non-null
Callback probe (p12UMCallbacks->pfnSetErrorCb): Attempted=1 Completed=1 ExceptionCaught=0 ExceptionCode=0x00000000
```

**The callback completed normally, with no SEH exception caught.** This is
the first direct evidence that a thunk-service callback supplied through
this owned-identity, no-kernel-driver-installed path is not merely a
non-null pointer but is genuinely *live and callable* -- the stock runtime's
own error-reporting machinery accepted a real call from this LLVM-owned
`d3d10warp.dll`. This remains narrowly scoped: it says nothing yet about
whether the kernel-routed (`pKTCallbacks`) allocation/residency callbacks are
similarly live, since those were deliberately not invoked here, and it says
nothing about whether a real resource/allocation contract could be built on
top of this -- only that the one pure user-mode diagnostic callback this
experiment chose to call did not fault.

**2. Newer-pinned-version control (`pinned-version-newer` variant, pins
`D3D12DDI_SUPPORTED_0119`, the newest `D3D12DDI_SUPPORTED_0NNN` macro in this
WDK's `d3d12umddi.h`, "Interface Version R8").** Same two caps answered
identically; everything else unchanged.

Measured result, container inbox runtime, repeated 3 times (same `ctest`
run; 9/9 passed):

```text
Bootstrap calls: Open=1 Size=0 Versions=2 Caps=1 Optional=0 Fill=0 Create=0 Close=1 Destroy=0
GetCaps Type values queried (1 total): 1074
CreateDevice args: Interface=0 Version=0 pKTCallbacks=null p12UMCallbacks=null
```

**`CreateDevice` is not reached (`Create=0`), and notably `CalcPrivateDeviceSize`
is not reached either (`Size=0`)** -- the runtime stops silently right after
`GetCaps` succeeds, the same single cap query as the working case, with no
additional cap queries logged and no fallback to an older interface version
attempted. This directly answers this session's control question: reaching
`CreateDevice` with this minimal two-cap implementation is **not** simply "any
pinned version works once the observed caps are answered" -- it is specific
to (at least) the older `D3D12DDI_SUPPORTED_0010` version tested so far. The
`D3D12DDI_ADAPTERFUNCS` table itself is unversioned (checked: its six
function-pointer fields are unconditionally defined, not gated behind any
`D3D_UMD_INTERFACE_VERSION` macro), so the divergence is not caused by the
adapter-function table shape; its cause is not yet established. A natural
bounded follow-up is bisecting across the intermediate
`D3D12DDI_SUPPORTED_0NNN` values to find where this behavior changes,
rather than guessing at a cause.

Both new variants are permanent CTest entries (`os-routed-callback-probe`,
`os-routed-pinned-version-newer`), both using the existing
`--warp-bootstrap-audit` probe mode. At the time of this measurement, neither
asserted a specific value for the
new evidence fields (consistent with the audit-only philosophy already
established for this harness) beyond a basic internal-consistency check (if
the callback probe ran at all, it must report either completing or having
been caught by SEH, never neither). The VM follow-up below tightens the
callback variant to require a completed, exception-free invocation and the
observed device-removal result.

### W0(g) public DDI-version matrix (VM measurements)

The application-local probe project
[`windows-warp-interposition`](../utils/windows-warp-interposition/CMakeLists.txt)
now accepts a semicolon-separated list of public
`D3D12DDI_SUPPORTED_00NN` macros through `FEME_PROBE_DDI_VERSIONS`. Each entry
builds an owned audit shim and an `os-routed-pinned-version-custom-NNNN`
CTest, plus a paired `-wrong-stage` negative control.
`FEME_PROBE_DDI_MATRIX_ONLY=ON` omits the unrelated WARP-forwarding
controls, so this matrix does not require the pinned NuGet WARP binary. The
matrix remains an audit stub: it cannot establish a working device or
resource lifecycle.

The matrix was built with MSVC 19.44.35230.0 for x64 and Windows SDK/WDK
10.0.26100.0, CMake 3.31.6, and Ninja 1.12.1, then run on Windows 11
Enterprise build 26300 (ARM64). The system inbox `d3d12.dll` and
`D3D12Core.dll` were both 10.0.26100.9818. The runtime selected
`Microsoft Basic Render Driver` with WARP identity
`vendor=1414`, `device=008c`, `flags=2`. Twenty-nine public versions were
tested: representative interface/build boundaries from 0010 through 0110,
plus every macro defined by this WDK from 0060 through 0089. All 29 original CTest cases passed as audit assertions, but the result varied
at two exact builds:

- Every tested version other than 0070 and 0071 reached
  `CalcPrivateDeviceSize` and `CreateDevice`; `CreateDevice` deliberately
  returned `E_NOTIMPL` (`0x80004001`). The counters were
  `Open=1, Size=1, Versions=2, Caps=1, Create=1, Close=1`.
- `D3D12DDI_SUPPORTED_0070` and `_0071` returned
  `DXGI_ERROR_UNSUPPORTED` (`0x887a0004`) before
  `CalcPrivateDeviceSize` or `CreateDevice` (`Open=1, Size=0, Versions=2,
  Caps=1, Create=0, Close=1`). Each was repeated three times with the same
  result; `_0072` was the adjacent successful negotiation control.

Thus, on this particular inbox runtime, the earlier 0119 failure is not a
general "newer DDI" cutoff: the unsupported point is limited to builds 0070
and 0071 in the tested range, with 0072 and later tested values reaching the
stub. The cause of this narrow rejection remains unknown. No allocation or
residency callback was invoked, and `E_NOTIMPL` is not device creation.

To reproduce, use the x64 Visual Studio developer environment configured for
the same installed SDK/WDK (`VsDevCmd.bat -arch=amd64 -winsdk=10.0.26100.0`),
and keep configure/build/test in that environment so the matching WDK include
path remains selected. Update the macro list to match the VM's `d3d12umddi.h`:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = 'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$repo = 'C:\Users\cbieneman\dev\llvm-project\feme'
$build = 'C:\Users\cbieneman\dev\warp-ddi-matrix'
$ddiVersions = @(
  'D3D12DDI_SUPPORTED_0010', 'D3D12DDI_SUPPORTED_0020',
  'D3D12DDI_SUPPORTED_0030', 'D3D12DDI_SUPPORTED_0040',
  'D3D12DDI_SUPPORTED_0050', 'D3D12DDI_SUPPORTED_0060',
  'D3D12DDI_SUPPORTED_0061', 'D3D12DDI_SUPPORTED_0062',
  'D3D12DDI_SUPPORTED_0063', 'D3D12DDI_SUPPORTED_0064',
  'D3D12DDI_SUPPORTED_0065', 'D3D12DDI_SUPPORTED_0070',
  'D3D12DDI_SUPPORTED_0071', 'D3D12DDI_SUPPORTED_0072',
  'D3D12DDI_SUPPORTED_0073', 'D3D12DDI_SUPPORTED_0074',
  'D3D12DDI_SUPPORTED_0075', 'D3D12DDI_SUPPORTED_0076',
  'D3D12DDI_SUPPORTED_0080', 'D3D12DDI_SUPPORTED_0081',
  'D3D12DDI_SUPPORTED_0082', 'D3D12DDI_SUPPORTED_0083',
  'D3D12DDI_SUPPORTED_0084', 'D3D12DDI_SUPPORTED_0086',
  'D3D12DDI_SUPPORTED_0088', 'D3D12DDI_SUPPORTED_0089',
  'D3D12DDI_SUPPORTED_0090', 'D3D12DDI_SUPPORTED_0100',
  'D3D12DDI_SUPPORTED_0110'
) -join ';'
& $cmake -S "$repo\utils\windows-warp-interposition" `
  -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe `
  -DCMAKE_SYSTEM_VERSION=10.0.26100.0 `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1" `
  -DFEME_PROBE_DDI_MATRIX_ONLY=ON `
  "-DFEME_PROBE_DDI_EXPECT_UNSUPPORTED=D3D12DDI_SUPPORTED_0070;D3D12DDI_SUPPORTED_0071" `
  "-DFEME_PROBE_DDI_VERSIONS=$ddiVersions"
& $cmake --build $build --target check-feme-windows-probes --parallel 2
```

The custom targets are the only targets in matrix-only mode, so a default
build compiles all versions in `$ddiVersions`. Preserve the exact WDK macro
list and environment details with each result; SDK-version mismatches can
silently test a different header/runtime combination.

### W0(g) strict negotiation gates and versioned callback audit (VM)

Follow-up on 2026-11-17, using the same VM and SDK/WDK 10.0.26100.0.
The audit now distinguishes an observed failure from an expected negotiation
stage:

- `--expect-create-device` requires one size calculation and one
  `CreateDevice`, the deliberate `E_NOTIMPL` result, matching pinned and
  negotiated 64-bit DDI versions, populated runtime callback tables and
  `pfnSetErrorCb`, and adapter close without device destruction.
- `--expect-unsupported` requires `DXGI_ERROR_UNSUPPORTED` before either
  device-size calculation or device creation, and adapter close.
- `--expect-callback` additionally requires exactly one completed
  `pfnSetErrorCb` invocation, no SEH exception, and
  `DXGI_ERROR_DEVICE_REMOVED` (`0x887a0005`). Reporting `E_NOTIMPL` through
  that callback changes the runtime result; it is not interchangeable with
  the callback-free stub's `E_NOTIMPL`.
- Strict modes require the module registered as `d3d10warp.dll` to be the
  exact module handle explicitly loaded for the audit, with no reference
  WARP forwarding dependency. Adapter identity alone is insufficient.

Custom matrix cases default to `--expect-create-device`. Only versions
explicitly listed in `FEME_PROBE_DDI_EXPECT_UNSUPPORTED`, a checked subset of
`FEME_PROBE_DDI_VERSIONS`, use the rejection expectation. Do not copy the
0070/0071 exception list to another runtime without measuring it. Each case
has a paired wrong-stage CTest which requires exit code 1 and the explicit
expectation-mismatch diagnostic, not merely any failure or DLL-load error.
The unqualified `--warp-bootstrap-audit DLL` remains available for discovery
and compatibility with older audit shims; it is not a strict regression gate.

The public callback audit also corrects an important interpretation:
`p12UMCallbacks` is **not exclusively user-mode diagnostics**. Its public
core-layer tables also contain kernel-service wrappers. The WDK's
`D3D12DDI_CORELAYER_DEVICECALLBACKS_0022` extends the older callback prefix
with allocation/deallocation entries. The shim reads those added entries
only when the negotiated DDI is at least `D3D12DDI_SUPPORTED_0022`.

| Negotiated DDI samples | Legacy KT allocate/deallocate | KT `Deallocate2` | Core-layer 0022 allocate/deallocate |
|---|---|---|---|
| 0010, 0020 | Both non-null | Non-null | Not read: older contract |
| 0022, 0072, 0110 | Both null | Null | Both non-null |

These are pointer observations, not invocation evidence. In these samples,
KT `MakeResident`, `Evict`, `CreatePagingQueue`, `DestroyPagingQueue` and
`MapGpuVirtualAddress` were non-null. Core-layer `MakeResident`/`Evict`
were non-null, while its paging-queue entries were null. This identifies
the public, versioned allocation surface to investigate next; a null legacy
KT entry is not proof that allocation is unavailable. At this stage no
kernel-service callback was invoked and no allocation or real device was
created; the paging-queue follow-up below adds invocation evidence separately.

Validation used MSVC 19.44.35230.0, warnings as errors, assertions and
disabled PCH:

| Run | Result |
|---|---|
| x64 inbox, original 29 DDI versions with explicit 0070/0071 rejection expectations and paired negative controls | 58/58 CTests, each repeated three times |
| Native ARM64 inbox, 0010/0022/0070/0071/0072/0110 with paired negative controls | 12/12 CTests, each repeated three times |
| Native ARM64 Agility 619 (`D3D12Core.dll` 1.619.5.0), same six versions through the separately built Agility probe | All six strict expectations passed; loaded core path verified under `AgilitySDK` |
| Native ARM64 callback-enabled 0010 shim, inbox and Agility 619 | Both strict callback checks passed; completed once, no exception, device-removal result |
| Original standalone probe controls, x64 inbox and native ARM64 Agility | 4/4 CTests in each build |
| Direct, non-OS-routed adapter-table regression | 1/1 CTest |

For native builds use `VsDevCmd.bat -arch=arm64 -host_arch=amd64` and a
separate build directory. Match the shim, harness and Agility core
architectures. The available local Agility 619 binary was ARM64; no x64
Agility result is claimed here. The callback-only control was built with
`FEME_BOOTSTRAP_PROBE_CALLBACK=1` and run with `--expect-callback`, separately
from the callback-free version matrix.

Both native runtimes reject 0070/0071 at the same stage as the x64 inbox
matrix, while 0072 reaches the stub. Thus the rejection is not limited to
x64 emulation or the inbox runtime. The public header describes 0070/0071
as R7 builds 0/1 and 0072 as R7 build 2 with revised `AddToStateObject`
DDIs; that is a version boundary, **not an established cause** of rejection.
The exact runtime reason remains unknown. W0(g) stays open for that
explanation and, more importantly, an owned device and kernel-service
resource lifecycle through the unmodified WARP kernel-mode driver.

### W0(g) public kernel paging-queue lifecycle (VM)

Follow-up on 2026-11-17, on the same Windows 11 Enterprise build 26300 ARM64
VM, using MSVC 19.44.35230.0 and SDK/WDK 10.0.26100.0. An explicitly
enabled, LLVM-owned shim now invokes the runtime-supplied
`pKTCallbacks->pfnCreatePagingQueueCb` and `pfnDestroyPagingQueueCb`
during its deliberately failing `CreateDevice`.

The public contracts used are
[D3D12DDIARG_CREATEDEVICE_0003](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d12umddi/ns-d3d12umddi-d3d12ddiarg_createdevice_0003),
[pfnCreatePagingQueueCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_createpagingqueuecb),
[D3DDDICB_CREATEPAGINGQUEUE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-d3dddicb_createpagingqueue),
and [pfnDestroyPagingQueueCb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_destroypagingqueuecb).
The shim passes `hRTDevice.handle`, a zero-initialized create structure with
normal priority and physical adapter index zero, and no private driver data.
On successful creation it records a nonzero paging-queue handle, a nonzero
sync-object handle and a non-null CPU fence mapping. It then destroys the
queue using the returned queue handle. The public destruction contract
also invalidates the associated sync object and fence mapping.

Both callbacks returned `S_OK` for each of the four versions that reached
`CreateDevice`:

| Runtime / architecture | DDI versions | Result |
|---|---|---|
| Inbox 10.0.26100.9818, x64 process | 0010, 0022, 0072, 0110 | Create/destroy both `S_OK`, all three outputs present, no SEH exception; each repeated three times |
| Inbox 10.0.26100.9818, native ARM64 | Same four | Same result; each repeated three times |
| Agility 619, core 1.619.5.0, native ARM64 | Same four | Same result; each repeated three times; actual loaded core path verified under `AgilitySDK` |
| All three configurations | 0070, 0071 | Expected `DXGI_ERROR_UNSUPPORTED` before `CreateDevice`; no paging callback attempted; each repeated three times |

Representative successful telemetry:

```text
Kernel paging queue probe: Enabled=1 CreateAttempted=1 CreateReturned=1 CreateResult=0x00000000 HasPagingQueue=1 HasSyncObject=1 HasFenceMapping=1 DestroyAttempted=1 DestroyReturned=1 DestroyResult=0x00000000 ExceptionCode=0x00000000
```

The shim returns `E_NOTIMPL` after a successful round trip. There is still
no owned device table, successful `D3D12CreateDevice`, buffer/texture
allocation, residency operation or command submission. The fence mapping is
not dereferenced, and none of the returned values is used after destruction.
This proves invocation and a bounded lifecycle through public runtime
kernel-service callbacks under the selected WARP identity, not the complete
WARP-kernel resource contract or an independently traced miniport call.
No system DLL, driver installation, signing or registry change was required.

The experiment is default-off. `FEME_PROBE_PAGING_QUEUE=ON` applies only to
the configured custom DDI matrix and selects `--expect-paging-queue`.
That runner mode requires the existing strict negotiation/provenance checks
plus both callback calls returning `S_OK`, all three outputs present and no
exception. It rejects a read-only shim rather than treating pointer
availability as successful execution. Explicitly rejected versions retain
their pre-device rejection expectation. Run the error-reporting callback
probe separately: setting a device error is intentionally incompatible with
this successful lifecycle experiment.

Failures are observable and do not become successful audit results.
The shim propagates callback failure HRESULTs and records SEH exception
codes. It does not destroy a queue after a failed create, but always attempts
destruction after a successful create with a valid queue handle, even if the
sync handle or fence mapping is missing. Incomplete outputs, non-`S_OK`
success codes and failed/exceptional destruction fail the strict check.
Separate mock-only fixtures exercise those paths without kernel calls.

Validation:

- The six-version x64 and native inbox matrices each passed 12/12 CTests
  (six expected outcomes and six wrong-stage controls), each repeated three
  times. The six native Agility outcomes also passed three repetitions.
- Both architectures passed all 14 mock scenarios plus the existing direct
  adapter-table regression (15/15 CTests each). Scenarios include missing
  callbacks/device/table, create OOM, destruction failure, missing outputs,
  exceptions in either callback, and unexpected `S_FALSE` results.
- The default-off x64 29-version matrix retains its prior negotiation
  checks and adds disabled-paging negative controls for versions reaching
  `CreateDevice` (85/85 CTests); a read-only shim must fail
  `--expect-paging-queue`.
- The four original reference-WARP/software-WARP/rejection/missing-module
  controls passed on native Agility 619.

To reproduce the opt-in matrix, use the matching Visual Studio developer
environment (`VsDevCmd.bat -arch=arm64 -host_arch=amd64
-winsdk=10.0.26100.0` for native ARM64, or `-arch=amd64` for x64) and a
separate build directory:

```powershell
$repo = 'C:\Users\cbieneman\dev\llvm-project\feme'
$build = 'C:\Users\cbieneman\dev\warp-paging-probe'
$versions = @(
  'D3D12DDI_SUPPORTED_0010', 'D3D12DDI_SUPPORTED_0022',
  'D3D12DDI_SUPPORTED_0070', 'D3D12DDI_SUPPORTED_0071',
  'D3D12DDI_SUPPORTED_0072', 'D3D12DDI_SUPPORTED_0110'
) -join ';'
cmake -S "$repo\utils\windows-warp-interposition" -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe `
  -DCMAKE_SYSTEM_VERSION=10.0.26100.0 `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1" `
  -DFEME_PROBE_DDI_MATRIX_ONLY=ON -DFEME_PROBE_PAGING_QUEUE=ON `
  "-DFEME_PROBE_DDI_VERSIONS=$versions" `
  "-DFEME_PROBE_DDI_EXPECT_UNSUPPORTED=D3D12DDI_SUPPORTED_0070;D3D12DDI_SUPPORTED_0071"
cmake --build $build --parallel 2
cmake --build $build --target check-feme-windows-probes --parallel 2
```

The standalone `windows-adapter-bootstrap` project builds the mock fixtures.
For Agility, use the separately built `windows-adapter-probe` with its
documented core/version options and a matching-architecture shim; run
`--warp-bootstrap-audit <shim-path> --expect-paging-queue` for each accepted
version and `--expect-unsupported` for the two rejected controls.

W0(g) remains open. The next resource gate is a public allocation/
deallocation and residency lifecycle, using the version-appropriate
callback surface, followed by a real owned device. Do not infer either from
this paging-queue result or introduce private WARP allocation data without
separate approval. The exact 0070/0071 rejection reason also remains unknown.

### W0(g) public KMT standard-allocation and residency control (VM)

The standalone LLVM-owned `utils/windows-kmt-allocation` diagnostic now
exercises a separate public OS allocation/residency control on the current
Windows 11 Enterprise build 26300 ARM64 VM. Both native ARM64 and emulated
x64 builds use MSVC 19.44.35230.0 and SDK/WDK 10.0.26100.0.
This executable uses DXGI and GDI/KMT, not D3D12: there is no inbox/Agility
core or negotiated UMD DDI selection to infer from these results.

The public UMD allocation structures (`D3DDDICB_ALLOCATE` and
`D3D12DDICB_ALLOCATE_0022`, including their allocation-info structures)
do not expose an allocation size or the KMT standard-allocation input.
An optional private driver-data pointer does not document WARP's allocation
encoding. The control therefore does **not** invent private data, cast a
runtime device handle to a KMT device handle, or pass allocations between
different devices.

Instead, it asserts DXGI WARP identity (`1414:008c`, software flag), opens
that adapter's LUID through `D3DKMTOpenAdapterFromLuid` and creates a
**separate** KMT device with zero device flags. It backs a single 64 KiB
`D3DKMT_STANDARDALLOCATIONTYPE_EXISTINGHEAP` with zero-initialized
`VirtualAlloc` memory and uses `D3DKMTCreateAllocation2`. Following the
[public create-allocation flag contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/ns-d3dkmthk-_d3dkmt_createallocationflags),
the flags are `StandardAllocation`, `ExistingSysMem`, `CreateShared`,
`CrossAdapter`, `CreateResource` and `NtSecuritySharing`.
`ExistingSection` is not combined with `ExistingSysMem`. Standard-allocation
flags, runtime/driver private-data pointers and sizes, and other allocation
flags are zero; in particular, **`NoKmdAccess=0`**. No shared NT handle is
exported. The documented standard-allocation `ExistingSysMem` case
explicitly accepts a user memory pointer; the general header comment about
user-mode `ExistingSysMem` alone is not the standard-allocation contract.

The control creates a normal-priority paging queue on that same KMT device,
makes the allocation resident, waits for asynchronous completion when
necessary, and evicts it. The observed outputs are:

| Operation | Native ARM64 | x64 |
|---|---|---|
| Open adapter / create KMT device | `STATUS_SUCCESS` / `STATUS_SUCCESS` | Same |
| Create standard allocation | `STATUS_SUCCESS`; nonzero resource/allocation handles | Same |
| Create paging queue | `STATUS_SUCCESS`; queue, sync object and fence mapping present | Same |
| Make resident | `STATUS_PENDING` (`0x00000103`); one allocation; paging fence `7001`; bytes to trim `0` | Same |
| Register asynchronous CPU fence wait | `STATUS_SUCCESS` | Same |
| Wait for paging event, 10000 ms bound | `WAIT_OBJECT_0` | Same |
| Evict | `STATUS_SUCCESS` | Same |
| Destroy allocation / paging queue / device / close adapter | All `STATUS_SUCCESS` | Same |

Fence values and LUIDs are observations, not pinned constants. A synchronous
`STATUS_SUCCESS` make-resident result is also accepted. All other operations
require exact `STATUS_SUCCESS`; unexpected informational statuses fail.
The allocation count and required handles/mapping must be present.
The mapped fence address is not dereferenced. For pending residency,
`D3DKMTWaitForSynchronizationObjectFromCpu` registers a process-owned event,
then `WaitForSingleObject` has a 10-second limit; a failed registration,
timeout or failed wait is an error, never successful residency.
Eviction is not attempted before completion. This proves OS acceptance
and completion of the public residency operations, not data correctness,
physical memory migration, GPU virtual-address mapping or command execution.

Cleanup attempts resource/allocation destruction, paging-queue destruction,
device destruction, adapter close and event/backing-memory release.
A valid resource or allocation returned with incomplete companion outputs
is still destroyed. Device destruction is attempted even after an earlier
cleanup failure, but the original failure still fails the test. Backing
memory is retained until process exit if device teardown fails rather than
freed while the device could retain it. Queue destruction owns the associated
sync object and fence mapping; they are not reused or separately destroyed.
All HRESULT/NTSTATUS stages and cleanup errors are reported.

Before the runner migration, each architecture passed **31/31 CTests, each
repeated three times**:
one real public lifecycle and 30 mock-only fixtures. Fixtures check required
flags, zero private data, same-device handles and cleanup call order;
they cover synchronous/pending success, open/device/allocation/queue/
residency/wait/eviction failures, incomplete outputs, unexpected positive
statuses, wait timeout/failure, teardown failures and backing-memory
retention. Mock fixtures never invoke KMT; their backing memory/event are
ordinary process-owned Win32 objects.

To reproduce, enter the matching Visual Studio developer environment
(`VsDevCmd.bat -arch=arm64 -host_arch=amd64 -winsdk=10.0.26100.0` for
native ARM64, or `-arch=amd64` for x64), then use a separate build directory:

```powershell
$repo = 'C:\Users\cbieneman\dev\llvm-project\feme'
$build = 'C:\Users\cbieneman\dev\warp-kmt-allocation'
cmake -S "$repo\utils\windows-kmt-allocation" -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe `
  -DCMAKE_SYSTEM_VERSION=10.0.26100.0 `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1"
cmake --build $build --parallel 2
cmake --build $build --target check-feme-windows-probes --parallel 2
& "$build\feme-windows-kmt-allocation.exe"
```

W0(g) remains open. This control does not load the owned UMD, invoke its
runtime allocation callbacks, implement an owned D3D12 heap/resource, create
a working owned D3D12 device or independently trace a miniport call.
The next integration gate is a documented resource lifecycle associated
with the **runtime-owned** device, not reuse of these separate-device handles.
Private WARP allocation ABI still requires separate approval. No system
binary, kernel driver, registry, signing or installation change was made.

### W0(g) LIT test-runner correction

All four standalone projects (`windows-adapter-probe`,
`windows-adapter-bootstrap`, `windows-warp-interposition` and
`windows-kmt-allocation`) now share LIT registration through
`utils/windows-probe-tests/WindowsProbeTests.cmake`. There are no
`enable_testing`, `add_test`, `set_tests_properties` or CTest-module
registrations in FeMe's CMake files. The check target builds required
executables/DLLs before running LIT, including dependencies embedded in
the negative-control arguments. Generated tests are configuration-specific;
reconfiguring a DDI matrix removes obsolete generated cases.

The original success/rejection assertions remain in their harnesses.
LIT's `not` handles the missing-module case. Wrong-stage and disabled-paging
controls retain the explicit exit-code/diagnostic check, rather than passing
on any error. Each test has a 30-second limit through the LIT check target.
To repeat a suite, invoke the check target repeatedly; CTest's
`--repeat until-fail:3` is not a LIT option.

LIT validation on this VM:

| Suite | Result |
|---|---|
| Native ARM64 adapter reference/rejection/missing-module controls | 4/4 passed |
| Native ARM64 direct adapter and paging fixtures | 15/15 passed |
| Native ARM64 paging-enabled six-DDI matrix and wrong-stage controls | 12/12 passed |
| x64 default-off 29-DDI matrix, wrong-stage and disabled-paging controls | 85/85 passed |
| Public KMT allocation/residency and failure fixtures | 31/31 passed on native ARM64 and x64 |
| Reconfigure native matrix to two DDIs, paging disabled | 5/5 passed; no stale cases from the prior matrix |
| Shared-core LIT target | 90/90 passed |
| Bounded fuzz LIT target | 6/6 passed |
| Vulkan unit tests through `FeMe-Unit` LIT | 892 passed, 15 skipped, zero failures |

These are LIT results, distinct from the historical CTest counts above.
The Vulkan unit-test target already uses `add_feme_unittest` and
the `FeMe-Unit` GoogleTest LIT suite; it requires no CTest conversion.
The shared-core and bounded fuzz check targets also now enter through LIT,
and seed-corpus cases are included in the regular `check-feme` suite.
An additional full `check-feme` build reached LIT but was stopped after
several minutes with Runtime CPU GoogleTest shards still running. No
full-suite pass is claimed; the completed scoped results are listed above.

### W0(d) public buffer-copy and readback oracle (VM)

The 2026-11-18 follow-up adds opt-in resource-data tests to the LLVM-owned
`windows-adapter-probe` runner. The previous queue/fence test proves ordering
of a signal, not that any buffer data traverses a command list. The new
oracle provides that reference behavior for W1(a)'s future owned resource
path. It does not import the separate KMT control's allocations or assume
that a runtime callback accepts a KMT device/allocation handle.

The two new CLI modes are:

- `--warp-buffer-copy`: enumerate WARP, require vendor/device/software
  identity, create a device and require its adapter LUID to match.
- `--software-buffer-copy DLL`: explicitly load the requested software
  module, require WARP identity and matching device LUID, then also require
  the loaded canonical `d3d10warp.dll` handle to equal the requested module.

Both use only public D3D12 calls: create a 64 KiB default heap and placed
buffer, a 128 KiB committed upload buffer and a 64 KiB committed readback
buffer. Initialize the upload buffer's two halves with different,
offset-dependent byte patterns, including high-offset bits rather than a
pattern repeating every 256 bytes. Record three non-overlapping copies
into the placed buffer, transition it from `COPY_DEST` to `COPY_SOURCE`,
and copy its entire contents to readback:

| Region | Destination offset | Upload offset | Bytes |
|---|---|---|---|
| Prefix baseline | 0 | 0 | 257 |
| Patch | 257 | 65536 + 113 | 60013 |
| Tail baseline | 60270 | 60270 | 5266 |

The copy queue executes the closed copy list, signals a fence and waits
at most 10000 ms. Only after completion does the probe map readback and
compare **all 65,536 bytes** against the CPU oracle, including both guards
and patch boundaries. Map read/write ranges are explicit. Successful runs
release resources, heap, allocator/list, queue, fence/event and device.
Fence/event creation and completion-event registration precede submission.
If submitted work's completion cannot be established, work resources,
queue/device and synchronization objects are retained until process exit
rather than released while work may still reference them. Failed operations
and validation mismatches return failure; no device-loss recovery is claimed.
The failure-retention policy is not a pending-work fault-injection test.

`ExpectBufferCopy.cmake` requires exit zero **and** the exact full-coverage
completion marker. A successful old queue-only test, load failure or
incomplete copy cannot satisfy that assertion. Nine CPU-only fixtures test
valid data, wrong length, a missing mapping, corruption in the prefix,
first/last patch bytes, first tail byte and final buffer byte, plus fixed
pattern values independent of the checker. They are data-oracle controls,
not mocks of D3D12 failure behavior.

Validation uses Windows 11 Enterprise build 26300 ARM64, MSVC 19.44.35230.0
and SDK/WDK 10.0.26100.0:

| Configuration | LIT outcome |
|---|---|
| x64 inbox runtime 10.0.26100.9818 | 15/15, repeated three times |
| Native ARM64 inbox runtime 10.0.26100.9818 | 15/15, repeated three times |
| Native ARM64 Agility 619 core 1.619.5.0.20260730.3 | 15/15, repeated three times |
| Shared-runner x64 six-DDI negotiation regression | 16/16 |

Each 15-test suite contains the four original adapter controls, the two
real buffer-copy paths and nine CPU fixtures. Agility runs reported the
application-local `AgilitySDK\D3D12Core.dll`; inbox runs reported the system
core, and all reported system WARP. The six-DDI regression covers
0010/0022/0070/0071/0072/0110, expected 0070/0071 rejection, wrong-stage
controls and disabled-paging controls. These results do not replace the
earlier 29-version matrix or imply that its unexplained rejection is fixed.

Use the existing standalone configure instructions and matching developer
environment/Python with LIT timeout support, then run:

```powershell
cmake --build $build --target check-feme-windows-probes --parallel 2
& "$build\feme-windows-adapter-probe.exe" --warp-buffer-copy
& "$build\feme-windows-adapter-probe.exe" --software-buffer-copy `
  "$env:SystemRoot\System32\d3d10warp.dll"
```

For Agility, configure with the existing matching-architecture
`FEME_PROBE_AGILITY_CORE` and `FEME_PROBE_AGILITY_VERSION=619` options.
No new shader compiler, binary fixture, private WARP ABI, installed driver
or system change is required. W0(g) still needs an owned runtime/DDI
resource lifecycle and a real FeMe device; this is reference-WARP coverage,
not a FeMe pass or closure of W0(d)'s owned-device provenance gate.

### W0(d) legacy compute dispatch/readback oracle (VM, 2026-11-20)

The reference adapter probe now executes the original
[`fxc-legacy-compute.hlsl`](../test/Translate/DXBC/fxc-legacy-compute.hlsl)
through stock D3D12, not a private WARP interface. Its public entry paths are:

- `--warp-compute-shader SHADER {0|7}`: enumerate and identity-check WARP.
- `--software-compute-shader DLL SHADER {0|7}`: select the requested
  software module and check WARP identity and loaded-module handle identity.

Both paths create a compute PSO with a root raw-UAV descriptor at `u3`,
using space 0 for `cs_5_0` and space 7 for `cs_5_1`. A 64 KiB placed UAV
buffer is initialized through an upload buffer, transitioned for compute,
dispatched as 32 groups of eight threads, transitioned for copy and copied
to readback. Fence completion precedes mapping and verification.
The independent scalar expectation is
`(index * 17 + 23) ^ (index >> 3)` for 256 little-endian uints; all
64,512 remaining bytes must retain the baseline pattern. The wrapper
requires exit zero and the exact full-coverage completion marker.

Nine CPU fixtures cover valid output, short/oversized and missing data,
first/last result corruption, first/last guard corruption, an untouched
buffer representing no dispatch, and independently anchored pattern values.
Missing shader paths and invalid register spaces fail explicitly.
Submitted resources are retained until process exit if completion or
synchronization teardown fails, following the buffer-copy probe's policy;
this is not device-loss recovery or failure-retention fault injection.

#### DXIL admission findings and public validation

The real FXC DXBC inputs executed first, but all four translated DXIL cases
initially failed `CreateComputePipelineState` with `0x80070057`. The public
SDK `IDxcValidator` exposed two serialization defects, fixed rather than
reclassified as expected failures:

1. FeMe's private `!feme.signature` reflection record leaked into DXIL.
   The validator rejected metadata not used by DXIL. The shared target
   backend now removes this record only for DXIL serialization; other
   targets retain their signature/reflection behavior.
2. LLVM selected the PSV ABI from the validator version only for populated
   graphics signatures. Empty-signature compute shaders instead emitted
   PSV v3 with a 52-byte runtime-info structure despite declaring validator
   1.0, which requires PSV v0's 24-byte structure. PSV version selection
   now applies independently of signatures. Portable LIT coverage checks
   version boundaries through 1.8 and the missing/disabled-version defaults;
   existing validator-1.7 compute fixtures now assert PSV v2.

After these fixes, the SDK validator accepts the shader module. **Bare
FeMe export still fails runtime PSO admission.** The new standalone
`feme-dxil-validator SDK-DLL-DIRECTORY INPUT [OUTPUT]` diagnostic uses the
public SDK factories and validation API. When output is requested, it uses
`DxcValidatorFlags_InPlaceEdit`, requires successful operation status and
writes the returned validated/stamped container. That container executes
correctly on reference WARP. This is public shader validation, not driver
signing, a fabricated checksum, disabled validation or an installation step.
The diagnostic prints requested/loaded SDK module paths and validation
status, checks input/output sizes and writes, and releases COM objects
before unloading the SDK libraries.

The opt-in LIT workflow compiles HLSL with FXC, exports with the explicitly
selected FeMe executable, validates/stamps DXIL with the matching-architecture
SDK DLLs, then demands byte-exact dispatch/readback. It does not silently
substitute the original DXBC for translated DXIL. Separate negative controls
require wrong-root-space DXBC and unvalidated DXIL to fail at PSO creation.
Validator argument, missing-module, missing-input and malformed-input
controls also run through LIT.

#### Measured configurations and reproduction

Windows 11 Enterprise ARM64 build 26300, MSVC 19.44.35230.0 and
SDK/WDK 10.0.26100.0 were used. The selected SDK `dxcompiler.dll` and
`dxil.dll` report file version **1.9.2602.17**.

| Configuration | LIT outcome |
|---|---|
| Native ARM64 inbox core 10.0.26100.9818 | 40/40 |
| Emulated x64 inbox core 10.0.26100.9818 | 40/40 |
| Native ARM64 Agility 619 core 1.619.5.0.20260730.3 | 40/40 |
| Default native ARM64 configuration, no optional shader tools | 28/28 |
| Related LLVM PSV/container, FeMe DXIL/DXBC and backend regressions | 289 passed, 1 Unsupported |

Each 40-case matrix includes eight genuine compute successes:
two legacy profiles, DXBC/validated-DXIL input and both public adapter
entry paths. It also includes the prior 15 adapter/buffer-copy controls,
nine compute CPU fixtures, two input negatives, four validator negatives
and two PSO-rejection controls. Agility reports the application-local
`AgilitySDK\D3D12Core.dll`; inbox reports the system core. All select
Microsoft WARP, not the owned rejection shim.
The 28-case default does **not** run optional shader-dispatch coverage.
Without FXC on `PATH`, the two portable focused compiler tests pass and
the two real-FXC import/export tests are explicitly Unsupported.

Starting from the existing standalone configuration and matching developer
environment/Python with LIT timeout support:

```powershell
$sdkTools = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\arm64"
cmake -S feme\utils\windows-adapter-probe -B $build `
  "-DFEME_PROBE_FXC=$sdkTools\fxc.exe" `
  "-DFEME_PROBE_FEME=$PWD\build-rel\bin\feme.exe" `
  "-DFEME_PROBE_DXIL_VALIDATOR_DIR=$sdkTools"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

Use SDK `x64` tools for an x64 probe build. Agility additionally needs the
existing matching-architecture core/version options. Supplying FeMe
requires FXC or DXC and a validator directory containing both SDK DLLs; bad
configuration fails explicitly. No shader fixture binaries are committed.

This is a scoped W0(c)/W0(d) reference oracle and W1(b) compiler/runtime
admission result. It does not close full legacy compatibility, descriptor
array/non-uniform binding coverage or graphics-stage execution. Owned
`CreateDevice` remains deliberately `E_NOTIMPL`; W0(g)/W1(a)'s owned
device/resource and shader-DDI/PSO integration remain open. A FeMe-translated
shader running on Microsoft WARP is not an owned FeMe D3D12 device pass.

### W1(b) grouped SM6 compute oracle (VM, 2026-11-20)

The compute probe now also accepts an optional `linear|grouped` workload
argument on both public shader modes; omission preserves the original
linear dispatch and completion marker. Unknown workloads fail explicitly.
The original
[`feme-dxc-grouped-compute.hlsl`](../test/Tools/feme/feme-dxc-grouped-compute.hlsl)
uses real DXC `cs_6_0` output and the same raw UAV `u3`, in spaces 0 and 7.
This checkpoint measured the SM6 path only; the
[legacy grouped follow-up](#w1b-legacy-grouped-compute-follow-up-vm-2026-11-20)
below records the later DXBC groupshared/synchronization result.

The grouped workload dispatches `(2,2,2)` groups of `(8,4,2)` threads,
giving a `(16,8,4)` dispatch-ID grid and 512 distinct output words. It
initializes 64 groupshared uints per group, synchronizes, reads the thread
at flattened local index `local ^ 32`, synchronizes before reusing that
storage, writes the neighbor value plus `101 * group`, synchronizes
again and reads `local ^ 1` for output. The middle barrier protects reads
from concurrent overwrites; this is not merely three redundant fences.

The scalar checker independently reconstructs group/local indices from
each output's 3-D coordinates. With flattened group `g`, local index `l`
and `p = 64*g + (l ^ 33)`, the result must be
`((p*17 + 23) ^ (p >> 3)) + 101*g`. Every byte outside the first 2,048
must retain the baseline pattern: **63,488 guard bytes** are checked.
The strict wrapper also requires the exact grouped-dimensions marker.
Nine additional CPU fixtures cover the grouped oracle's size, mapping,
result/guard corruption and no-dispatch controls, with independent anchors
including results 0, 8, 16, 128 and 511. These are data-oracle tests, not
device-failure injection.

#### Modern-bitcode metadata admission fix

Direct DXC shaders executed correctly, but all four FeMe-re-emitted
grouped cases initially failed SDK validation with
`All metadata must be used by dxil`, naming a `!guid` node.
LLVM's bitcode reader upgrades older bitcode by attaching modern GUID
metadata to definitions for linking/cloning. LLVM DXIL preparation now
removes those attachments from global objects before frozen DXIL emission,
without discarding other metadata or changing non-DXIL targets. The portable
[`strip-guid-metadata.ll`](../../llvm/test/CodeGen/DirectX/strip-guid-metadata.ll)
regression failed before the fix and now verifies GUID removal from both
a function and a global, while retaining the function's debug scope/location.

The HLSL compiler regression compiles both spaces, re-emits through FeMe,
re-imports/verifies the actual payload, and checks shared-memory accesses,
three ordered barriers, group/flattened IDs, bindings and dimensions.
It uses `system-dxc-dxil`/`%dxil_dxc`, independently of the existing
SPIR-V-capability-gated `system-dxc` feature. Without DXC the HLSL test is
Unsupported, not a pass; the GUID regression remains portable.

#### Validation and reproduction

The same VM, compiler, SDK and runtime versions as the preceding oracle
were used. All automated coverage runs through LIT, not CTest.

| Configuration | LIT outcome |
|---|---|
| Native ARM64 inbox, FXC/DXC/FeMe enabled | 59/59 |
| Emulated x64 inbox, FXC/DXC/FeMe enabled | 59/59 |
| Native ARM64 Agility 619, FXC/DXC/FeMe enabled | 59/59 |
| Native ARM64 DXC/FeMe enabled, no FXC | 49/49 |
| Default native ARM64, no optional shader tools | 39/39 |
| Targeted LLVM DXIL preparation and FeMe export/raising regressions | 59/59 |

The full matrices retain the preceding 40 cases and add eight actual grouped
dispatch successes (two spaces, direct-DXC/validated-FeMe input and both
adapter paths), nine CPU controls and two invalid-workload controls.
The default matrix runs no optional shader dispatches. SDK validation and
runtime module paths remain visible in the workflow diagnostics.

Enable grouped coverage alongside the existing configuration:

```powershell
cmake -S feme\utils\windows-adapter-probe -B $build `
  "-DFEME_PROBE_DXC=$sdkTools\dxc.exe"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

DXC can be supplied without FXC. FeMe re-emission still requires the explicit
FeMe executable and matching-architecture SDK validator directory; omitting
FeMe runs only direct-DXC reference cases. The owned device is unchanged.
This measures a 64-thread grouped workload, not an exact wave size or
partial-wave policy. Broader barriers, repeated execution, concurrent owned
PSO/context lifetimes remain open.

### W1(b) legacy grouped compute follow-up (VM, 2026-11-20)

The same original grouped HLSL now runs through real SDK FXC `cs_5_0`
and `cs_5_1`, both as original DXBC and as SDK-validated/stamped FeMe DXIL.
The SM5.0 profile uses `u3` in space 0; SM5.1 uses space 7. Each profile
and input kind runs through default WARP and explicit software-reference
entry paths. These eight additional cases preserve all preceding checks:
512 scalar results, all 2,048 output bytes, all 63,488 guard bytes and
strict dispatch/copy/fence/readback completion.

DXBC translation now declares raw/structured shared regions as aligned
32-bit arrays in address space 3, without resource handles. It preserves
structured strides, byte offsets, load masks, view/source swizzles and
float/integer bit patterns. Shared modules carry DXIL's data layout before
optimization so GEP indices stay 32-bit. Duplicate regions, a combined
allocation above 32 KiB, undeclared/mismatched accesses, invalid registers
or stages, statically misaligned offsets, invalid store masks and mapping
feedback produce explicit diagnostics.
The shared DXIL raiser also reconstructs scalar i32/f32 bitcasts needed
when typeless shared words feed floating-point arithmetic.
The portable mixed-region export passes public SDK `IDxcValidator`
validation (`S_OK`), including both directions of bit reinterpretation.

All eleven nonempty, nonconflicting DXBC synchronization combinations
translate to the exact DXIL barrier bits. Standard HLSL barrier modes,
including the oracle's group-memory/thread synchronization mode 9, use
existing raising/lowering. The remaining thread-only/group-UAV modes
(1, 4, 5, 12, 13) are not yet raised for export or CPU execution; DXIL
export reports unsupported unraised operations instead of reaching the
backend assertion. No stronger fence or CPU synchronization semantics are
silently substituted.

The new portable LIT tests cover raw/structured regions, nonzero byte
offsets and 20-byte strides, swizzles, load/store masks, bit reinterpretation,
the exact 32-KiB boundary, diagnostics and all eleven synchronization modes.
[`feme-fxc-grouped-compute.test`](../test/Tools/feme/feme-fxc-grouped-compute.test)
checks actual SM5.0/5.1 import/export, shared storage, all three barriers,
register spaces and `(8,4,2)` thread dimensions. The existing DXC fixture's
separate register-space and thread-dimension checks are restored.

Measured LIT outcomes:

| Configuration | LIT outcome |
|---|---|
| Native ARM64 inbox, FXC/DXC/FeMe enabled | 67/67 |
| Emulated x64 inbox, FXC/DXC/FeMe enabled | 67/67 |
| Native ARM64 Agility 619, FXC/DXC/FeMe enabled | 67/67 |
| Native ARM64 FXC/FeMe enabled, no DXC | 59/59 |
| Native ARM64 FXC only, no FeMe/DXC/validator directory | 48/48 |
| Default native ARM64, no optional shader tools | 39/39 |
| FeMe DXBC import/translation, DXIL transforms and CLI regressions | 199/199 |

With SDK FXC/DXC directories removed from `PATH`, all four new portable
tests pass and the two real-compiler grouped fixtures are **Unsupported**,
not counted as passes. Reconfiguration removes stale optional LIT cases.

The existing opt-in FXC/FeMe configuration automatically includes the
legacy grouped cases; no additional test runner or CTest registration is
needed. This is still reference-WARP execution, not an owned FeMe device
pass. W1(b), full legacy-stage coverage, actual wave policy, partial waves,
repeated execution and concurrent owned PSO/context lifetimes stay open.
Owned `CreateDevice` remains deliberately `E_NOTIMPL`.

### W1(b) partial-wave policy and repeated compute oracle (VM, 2026-11-20)

The new original
[`feme-dxc-partial-wave-compute.hlsl`](../test/Tools/feme/feme-dxc-partial-wave-compute.hlsl)
executes SM6.0 compute with **30 threads per group**, `(5,3,2)`, across
`(3,2,1)` groups. Each of its 180 threads writes an eight-word record:
a deterministic shared-memory neighbor value after a group barrier, the
flattened 3-D dispatch ID, wave size/lane index, first active lane's local
index, active count, sum of local indices plus one, and first lane's group.
The fixture uses eight scalar raw-buffer stores per record; it does not
add multi-component raw-store import support.

[`PartialWaveValidation.h`](../utils/windows-adapter-probe/PartialWaveValidation.h)
checks all 5,760 payload bytes and all **59,776 guard bytes**. Scalar
values and dispatch IDs are independently reconstructed. Wave membership
is grouped by reported first-local identity within each thread group,
not an assumed correspondence between local thread index and lane index.
It checks unique in-range lanes, consistent sizes/active counts/reductions,
complete membership, the first member's lowest active lane index, and
group isolation. Sizes must be powers of two in the public 4--128 range
and inside the queried runtime bounds. Thirty threads cannot fill an
integer number of such waves, so each group must contain a partial wave.
The public [WaveGetLaneCount contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/wavegetlanecount)
counts inactive lanes too; active count is measured separately.

The probe queries `D3D12_FEATURE_D3D12_OPTIONS1` and rejects unsupported or
invalid wave bounds. Each partial-wave test reuses one device, queue, PSO,
root signature, heap and upload/default/readback buffers for **three
completed dispatches**. It resets the allocator and command list only
after completion. Every iteration copies the baseline into the output
buffer before dispatch, preventing stale shader output from validating a
missing dispatch. The default buffer's next transition accounts for
[decay to COMMON after batch completion](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).
All records and guards are validated after every iteration, and the
wrapper requires all three completion markers. Existing linear/grouped
workloads retain single-dispatch behavior. Submitted-failure retention
is unchanged. The wait helper still creates a separate fence per
iteration; this is not monotonic reuse of one fence.

#### DXIL admission and independent controls

The real shader initially exposed a missing import mapping for
`dx.op.waveReadLaneFirst` (opcode 118). The table-driven raiser now maps
it to `llvm.dx.wave.readlane.first`, reusing existing export/lowering.
[`dxil-raise-wave-read-first.ll`](../test/Transforms/DXIL/dxil-raise-wave-read-first.ll)
checks i16/i32/i64 and f16/f32/f64 raising and LLVM verification without
DXC. The HLSL regression checks both register spaces, re-export/re-import,
wave operations, shared storage, barrier, bindings and thread dimensions.
Re-emitted runtime inputs require successful public SDK validation.

The standalone LIT matrix adds 24 CPU fixture scenarios and eight
reference dispatch cases: spaces 0/7, original DXC/validated FeMe DXIL,
and `EnumWarpAdapter`/explicit software paths. Synthetic success data
covers wave sizes 4, 8, 16, 32, 64 and 128, both direct and permuted
local/lane mappings, and partial waves whose first active lane is not
lane zero. Controls reject bad readback size/null data, scalar results,
IDs, wave ranges/consistency, duplicate lanes, missing/wrong first members,
active counts, reductions, group markers, guard corruption, untouched
baseline output and unsupported/incompatible runtime policies.

#### Measured results and reproduction

The inbox D3D12/core/WARP files are `10.0.26100.9818`; DXC is
`1.9.0.5399`, SDK FXC/validator files come from `10.0.26100.0`, and
Agility is the existing ARM64 619 core `1.619.5.0.20260730.3`.
Diagnostics verify the inbox module paths and application-local Agility
core path. All three shader-enabled configurations report `WaveOps=1`, min/max
**4/4**, and observes four-lane waves. Each iteration reconstructs
**48 waves, six partial**, including one two-active-lane tail per group.
These measurements are reference-runtime facts, not a selected owned
FeMe wave policy.

| Configuration | LIT outcome |
|---|---|
| Native ARM64 inbox, FXC/DXC/FeMe enabled | 99/99 |
| Emulated x64 inbox, FXC/DXC/FeMe enabled | 99/99 |
| Native ARM64 Agility 619, FXC/DXC/FeMe enabled | 99/99 |
| Default native ARM64, no optional shader tools | 63/63 |
| DXIL transforms and FeMe CLI regressions | 52 passed, 2 unsupported |
| Complete rebuilt FeMe suite | 3,996 passed, 15 skipped, 13 unsupported; zero failures; 69.29 s |

Reconfiguring the full native matrix without optional tools removes its
stale shader tests. All automated tests use LIT, including the CPU
fixtures; no CTest registration is present. Existing DXC/FeMe probe
configuration automatically includes these partial-wave tests.

For direct reproduction with a built probe and a compiled/validated shader:

```powershell
& "$build\feme-windows-adapter-probe.exe" --warp-compute-shader `
  $shader 7 partial
& "$build\feme-windows-adapter-probe.exe" --software-compute-shader `
  "$env:SystemRoot\System32\d3d10warp.dll" $shader 7 partial
```

The Vulkan ICD and all FeMe LIT prerequisites were rebuilt. Loader/graphics
smoke, Vulkan unit tests and inventory-generator regressions pass in the
complete suite. The capability-manifest gate also reproduces both
committed tables using their pinned Vulkan-Docs registry `f84d432d5b89`.
No Vulkan advertisement changes are made. No real CTS run is claimed:
this environment has no `deqp-vk` binary/checkout at the documented path.
See [the scoped regression note](VulkanCTSReport.md#w1b-reference-partial-wave-oracle-regression-scope-2026-11-20).

W1(b) and owned W0(g) execution remain open: owned `CreateDevice` is still
deliberately `E_NOTIMPL`. This does not establish requested `[WaveSize]`
support, owned PSO compilation/context/code concurrency, owned resource
allocation, or all-stage D3D12 compatibility.

### W1(b) FeMe CPU partial-wave execution (VM, 2026-11-20)

The preceding partial-wave reference shader now executes through FeMe's
own DXIL import, CPU compiler and `feme-run` executor. Original DXC
SM6.0 input and FeMe-re-emitted DXIL are each tested at widths
**4, 8, 16 and 32**, in register spaces **0 and 7**: sixteen CPU dispatch
cases using the same `(5,3,2)` threads and `(3,2,1)` groups as WARP.
This is actual FeMe CPU execution, not a reference-only validator result,
but it is still an internal CLI path rather than an owned D3D12 device.

[`ExpectFeMePartialWave.cmake`](../utils/windows-adapter-probe/ExpectFeMePartialWave.cmake)
compiles the original shader, optionally re-emits DXIL, generates a heap
initialized with the same 64 KiB baseline, executes the CPU dispatch and
passes the complete binding dump to the existing strict
[`PartialWaveValidation.h`](../utils/windows-adapter-probe/PartialWaveValidation.h)
oracle. Each run checks all 180 eight-word records and all 59,776 guard
bytes. Scalar shared-memory/3-D-ID results have the same expectations as
WARP; wave-dependent fields are checked against the requested CPU width,
not forced to match WARP's measured four-lane policy.

| CPU width | Observed waves per dispatch | Partial waves per dispatch |
|---|---|---|
| 4 | 48 | 6 |
| 8 | 24 | 6 |
| 16 | 12 | 6 |
| 32 | 6 | 6 |

The completion wrapper requires these exact counts as well as the register
space, 180 records and 59,776 guard bytes. CPU widths 64/128 remain covered
by synthetic validator fixtures, not by this bounded real-execution matrix.
These CLI width selections are not HLSL `[WaveSize]` attribute support or
advertised owned-D3D capabilities.

#### First-active-lane CPU support

The initial real CPU pilot failed with `unsupported divergent call to
'llvm.dx.wave.readlane.first.i32'`. The DXIL importer already raised
opcode 118, but the CPU pipeline did not classify or lower its dedicated
intrinsic. It now classifies the result as wave-uniform, represents it
as `feme.cpu.wave.readfirst` with a widened value and explicit active mask,
and extracts the lowest active lane's value without inventing a lane-index
operand. Vector values are decomposed component-wise. Linearization
preserves the call's operands and attaches the region's divergence mask;
widening combines that with the entry mask. Empty masks clamp the
extraction index and return zero, rather than introducing poison.

Portable LIT regressions check i16/i32/i64, f16/f32/f64 and Boolean
lowering, floating-point negative-zero bit preservation, inactive poison
values, empty masks and vector widening. A real DXC divergent-region
shader checks first-active lanes 2 and 6, not a hardcoded lane zero.
The named `WaveCallsTest.ReadFirstLaneIsScalarAndHasNoLaneIndex` helper
test also runs through LIT. No separate rasterizer, Vulkan-command
translation or D3D COM implementation is added.

#### Strict interchange controls

[`BufferIO.h`](../utils/windows-adapter-probe/BufferIO.h) provides
the baseline heap writer and exact runner-dump parser. Parsing accepts one
complete `binding[space:3][0]` dump with precisely 16,384 unsigned 32-bit
words. It rejects incorrect bindings, missing separators/words, overflow,
negative/non-numeric tokens, extra output and read errors. Partial parses
cannot become zero-filled success-shaped readbacks.

Eleven additional fixture scenarios exercise successful bit-exact parsing,
incorrect headers, truncation, extra words, overflow, negative/invalid
tokens, size/null arguments, and payload/guard corruption. Four CLI
controls reject invalid spaces/widths and missing output files. These
fifteen cases run even without shader tools.

#### Validation and reproduction

The VM, SDK and reference-runtime versions are unchanged from the
preceding measurement. All automated testing uses LIT, not CTest.

| Configuration | LIT outcome |
|---|---|
| ARM64 oracle, inbox WARP, all shader tools and CPU runner enabled | 130/130 |
| x64 oracle, inbox WARP, all shader tools and CPU runner enabled | 130/130 |
| ARM64 oracle, Agility 619 WARP, all shader tools and CPU runner enabled | 130/130 |
| Default ARM64, no optional shader tools/CPU runner | 78/78 |
| Complete rebuilt FeMe suite | 4,000 passed, 15 skipped, 13 unsupported; zero failures; 75.58 s |

The FeMe compiler/CPU runner is **native ARM64 in all three matrices**.
The x64 configuration exercises an emulated x64 validator/reference
harness, not an x64 FeMe backend. Agility applies to the reference D3D12
probe, not to the CPU executor process. Reconfiguration removes stale
optional CPU/shader tests.

Enable the CPU comparison alongside the existing probe configuration:

```powershell
cmake -S feme\utils\windows-adapter-probe -B $build `
  "-DFEME_PROBE_DXC=$dxc" `
  "-DFEME_PROBE_FEME_RUN=$llvmBuild\bin\feme-run.exe"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

With `FEME_PROBE_FEME` also supplied, both original and re-emitted input
cases run; without it, only original DXC CPU cases are generated. The
existing public-runtime re-emission matrix still requires its matching
SDK validator. CPU execution imports FeMe's bare re-emitted container
directly; it does not claim that this unstamped file is admitted as a PSO.
For bounded parallel validation, the same generated suite can be run
through LIT with `-j2 --timeout=30`.

The rebuilt Vulkan ICD passes the complete FeMe unit/loader/graphics
suite. Both capability tables reproduce unchanged against pinned
Vulkan-Docs registry `f84d432d5b89`; no advertisements change.
Real CTS remains unavailable and is not counted as passing coverage.
The [scoped regression note](VulkanCTSReport.md#w1b-feme-cpu-first-active-lane-regression-scope-2026-11-20)
keeps that limit separate from the passing tests.

Owned `CreateDevice` remains deliberately `E_NOTIMPL`. Owned resource
allocation, D3D12 PSO/context/code lifetime and concurrency, and owned
repeated dispatch remain open under W0(g)/W1. This milestone proves
portable compute behavior needed by that future frontend; it does not
close those device gates.

### W1(b) retained CPU dispatch and compilation-context lifetimes (VM, 2026-11-20)

The [preceding CPU comparison](#w1b-feme-cpu-partial-wave-execution-vm-2026-11-20)
now exercises retained code and resources rather than only one dispatch
per fresh process. `feme-run --repeat=N` compiles the JIT shader or loads
the AOT object once, runs N completed dispatches, and prints a complete
heap snapshot after **each** dispatch. The same executable, JIT worker
pool and buffer/image/sampler storage stay alive. The positive default
is 1, preserving existing single-dispatch output. Zero is rejected with
an explicit diagnostic before input loading. Resource contents are not
reset between iterations; `--reference` and `--object` use the same
repeat contract.

`ExpectFeMePartialWave.cmake` requests **three** dispatches in every
existing CPU case: original/re-emitted SM6 DXIL, spaces 0/7 and widths
4/8/16/32. It requires exactly three binding dumps and invokes the strict
oracle independently for every snapshot, not merely the last. Each
snapshot verifies 180 records, six partial waves, the exact
48/24/12/6 wave count for its selected width, and all 59,776 guard bytes.
The full-buffer baseline is initialized once and retained; unlike the
reference command-list probe, this CLI does not copy it again before each
dispatch.

Two stateful regression fixtures make actual replay observable:

- [`repeat-dispatch.ll`](../test/Tools/feme-run/repeat-dispatch.ll)
  increments six independent buffer words over two partial-wave groups.
  It checks three distinct full output lines and unchanged guard words,
  as well as default/explicit single-dispatch output and invalid zero
  repetition. The same fixture runs through normal JIT, scalar reference
  execution and the AOT object loader.
- [`repeat-partial-wave.test`](../test/Tools/feme-run/HLSL/repeat-partial-wave.test)
  compiles real DXC SM6.0 HLSL with seven threads in each of two groups,
  four-lane waves, group-shared storage, a group barrier, wave reduction
  and first-active-lane reads. Every iteration publishes its previous
  output into shared memory, then rotates it across full/partial waves.
  All fourteen results follow a checked three-step recurrence, with two
  guard words unchanged. The barrier separates all old-output reads from
  new writes. Skipped dispatches, stale snapshots, implicit resets and
  stale shared-memory phases cannot reproduce the expected sequence.

[`CompiledStageTest.ConcurrentCompilationAndRepeatedExecutionOutliveContexts`](../unittests/Target/CPU/CompiledStageTest.cpp)
adds a separate portable ownership test. Two independent compilation
contexts concurrently create stages at widths 4 and 8; both contexts and
their source modules are destroyed before execution. Each retained stage
then builds one prepared dispatch and reuses its arguments for three
rounds of four group invocations, concurrently with the other stage.
Per-round buffer reinitialization requires fresh stores, and both bounds
guards are checked. Errors from worker threads are captured and asserted
after joining. This tests independent contexts, not concurrent use of
one mutable LLVM context.

All tests run through **LIT**, never CTest. On the same Windows VM:

| Configuration | LIT outcome |
|---|---|
| ARM64 inbox reference/oracle + native ARM64 FeMe runner | 130/130, including 48 CPU dispatch readbacks |
| Emulated x64 inbox reference/oracle + native ARM64 FeMe runner | 130/130, including 48 CPU dispatch readbacks |
| ARM64 Agility 619 reference/oracle + native ARM64 FeMe runner | 130/130, including 48 CPU dispatch readbacks |
| Default ARM64 configuration after removing optional tools | 78/78 |
| Complete FeMe suite | 4,003 passed, 15 skipped, 13 unsupported; zero failures; 74.05 s |

The full suite discovers 4,031 tests. The three strengthened exact-output
fixtures also pass a focused rerun. Test-count changes are two new
source fixtures and one helper unit case; the existing AOT test receives
additional commands. The Windows matrix test count is unchanged because
each existing CPU case now performs three dispatches rather than one.
Reconfiguration still removes stale optional tests. LIT's standalone
per-test timeout needs `psutil`; it was installed in a task-local Python
environment after the system interpreter reported that missing dependency.

The [CPU CLI contract](FeMeCPUDesign.md#command-line) documents the option.
No shared compiler/runtime implementation, ABI or Vulkan advertisement is
changed by this follow-up; the existing ICD/graphics regression suite
passes. The [CTS scope note](VulkanCTSReport.md#w1b-retained-cpu-execution-regression-scope-2026-11-20)
does not claim external CTS coverage.

This remains internal portable-executor evidence. The x64 matrix does
not exercise an x64 FeMe backend, and Agility still applies only to the
reference D3D12 process. Owned `CreateDevice` is unchanged at `E_NOTIMPL`.
Real D3D12 PSO compilation/context lifetime, teardown with pending work,
resource retention across recorded commands, command-list replay and
fence semantics remain open under W0(g)/W1. The contexts in the unit test
are compiler contexts, not runtime-owned D3D12 command contexts.

### W1(b) legacy DXBC CPU execution (VM, 2026-11-20)

The [legacy shader compatibility audit](W0DirectXTestCorpusClassification.md#legacy-shader-compatibility-audit-vm-2026-11-18)
establishes the SM5.1 input obligation, but the earlier raw/grouped FXC
oracles executed only on reference WARP. They now also execute through
FeMe's own CPU compiler/executor, as **original FXC DXBC** and
**FeMe-re-emitted DXIL**. This is measured legacy compute execution, not
merely import/export success.

#### Shared import and arithmetic normalization

`feme-run` previously sent every `DXBC`-magic container to DXILImporter,
so real FXC input could not reach execution. It now shares
[`containsDXBCShader`](../include/feme/Import/DXBC/DXBCImporter.h) with
`feme::Driver`: inspect SHEX/SHDR parts, safely handle zero-part containers
and propagate malformed-container errors. Recognized legacy input goes
through DXBCImporter and DXSAToLLVMIRTranslator, followed by the same
DXIL operation/metadata raising as DXIL input. No second bytecode parser
or translation pipeline is introduced. The DXIL, SPIR-V and raised-LLVM
paths remain available.

The first real pilot then failed with `unsupported divergent call to
'llvm.dx.imad.i32'`. FXC's integer multiply-add raises to a DirectX
intrinsic that the CPU JIT did not normalize. Both the normal
[`CPU pipeline`](../lib/Target/CPU/Pipeline.cpp) and
[`scalar-reference path`](../lib/Target/CPU/CompiledStage.cpp) now run the
existing shared DXIL intrinsic-expansion pass before resource lowering
and widening. Signed/unsigned integer multiply-add becomes LLVM
`mul`/`add` without no-overflow assumptions; no alternate arithmetic
implementation is added. Context-free floating-point math expansion is
also shared with the existing non-DXIL export path. Context-dependent
wave/resource/derivative operations remain for their existing lowerings.

[`dxil-integer-mad.ll`](../test/Tools/feme-run/dxil-integer-mad.ll) checks
signed/unsigned overflow, negative operands and partial-wave guards in
normal and scalar-reference execution.
[`fxc-legacy-input.test`](../test/Tools/feme-run/HLSL/fxc-legacy-input.test)
checks real SM5.0 space0 and SM5.1 space7 inputs with three retained
dispatches and exact output lines. Importer unit coverage distinguishes
SHEX/SHDR from unrelated parts without parsing bytecode, checks malformed
input and safely declines empty/non-container input.

#### Real CPU matrix

[`ExpectFeMeLegacyCompute.cmake`](../utils/windows-adapter-probe/ExpectFeMeLegacyCompute.cmake)
compiles the existing LLVM-owned HLSL fixtures with SDK FXC `/WX` and
executes:

| Input profile/space | Workloads | Input encodings | CPU widths |
|---|---|---|---|
| SM5.0 / space0 | Raw and grouped | Original DXBC and re-emitted DXIL | 4, 8 |
| SM5.1 / space0 | Raw and grouped | Original DXBC and re-emitted DXIL | 4, 8 |
| SM5.1 / space7 | Raw and grouped | Original DXBC and re-emitted DXIL | 4, 8 |

This is **24 CPU cases**, each compiling once and validating **three**
completed dispatches with retained code/resources: 72 legacy readbacks
per full configuration. SM5.0 nonzero-space cases are not generated.
The two workload oracles are unchanged from reference WARP:

| Workload | Threads per group | Dispatch groups | Checked scalar results | Checked guard bytes |
|---|---|---|---|---|
| Raw UAV | `(8,1,1)` | `(32,1,1)` | 256 | 64,512 |
| Grouped UAV/shared memory | `(8,4,2)` | `(2,2,2)` | 512 | 63,488 |

The grouped shader has three group barriers, cross-wave shared-memory
exchange and flattened 3-D dispatch IDs. Every snapshot is a complete
65,536-byte binding dump, parsed and compared byte-for-byte with the
scalar expectations; validating only the last dispatch or only the
payload prefix is insufficient.

The former wave-only interchange is extracted as
[`BufferIO.h`](../utils/windows-adapter-probe/BufferIO.h), retaining its
exact register3/space0-or-7, unsigned-word, size, overflow and trailing-data
checks. Partial-wave fixtures continue to exercise all protocol-negative
controls. Raw/grouped validators reuse it rather than introducing a
second parser.
[`ValidateFeMeBufferOutput.cmake`](../utils/windows-adapter-probe/ValidateFeMeBufferOutput.cmake)
shares the exact-three-dumps/per-snapshot completion gate with the SM6
partial-wave matrix. Four new CLI controls reject invalid heap/output
spaces, an unknown workload and a missing output file; all existing
payload/guard-corruption controls remain passing.

#### Validation and reproduction

The reference runtimes and SDK FXC/validator versions remain those of the
preceding VM measurements. The native ARM64 FeMe compiler/runner is used
in **all** configurations; the x64 case exercises an emulated x64 oracle,
FXC and reference runtime, not an x64 FeMe CPU backend. Agility changes
the reference D3D12 process, not the CPU executor.

| Configuration | LIT outcome |
|---|---|
| Native ARM64 inbox, all shader tools | 158/158 |
| Emulated x64 inbox oracle/reference, all shader tools | 158/158 |
| Native ARM64 Agility 619, all shader tools | 158/158 |
| Native ARM64 FXC + CPU runner only, no DXC/exporter/validator | 103/103 |
| Native ARM64 default, no optional shader tools/runner | 82/82 |
| Complete rebuilt FeMe suite, native SDK FXC on PATH | 4,010 passed, 15 skipped, 10 unsupported; zero failures; 74.44 s |

The complete suite discovers 4,035 tests. Four new tests account for
two detection unit cases and two execution fixtures; explicitly enabling
FXC also makes three previously Unsupported legacy source tests pass.
Those infrastructure-dependent counts are not new conformance claims.
The rebuilt Vulkan ICD and existing unit/real-loader/graphics coverage
pass. The pinned Vulkan-Docs `f84d432d5b89` capability check runs through
LIT and reproduces both inventory tables unchanged. No advertisements
change, and no external CTS result is claimed; see the
[regression scope](VulkanCTSReport.md#w1b-legacy-dxbc-cpu-regression-scope-2026-11-20).

FXC-only CPU execution no longer requires DXC. From the LLVM checkout:

```powershell
cmake -S feme\utils\windows-adapter-probe -B $build `
  "-DFEME_PROBE_FXC=$fxc" `
  "-DFEME_PROBE_FEME_RUN=$llvmBuild\bin\feme-run.exe"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

Adding `FEME_PROBE_FEME` enables re-emitted CPU cases alongside the existing
reference export matrix, which still requires its matching SDK validator.
The CPU imports bare re-emitted DXIL internally; this does not claim stock
runtime PSO admission without SDK validation. Optional-test removal was
verified by reconfiguring through FXC-only and default matrices. Every
automated result above is **LIT**, not CTest.

Owned `CreateDevice` remains `E_NOTIMPL`, and no runtime-owned allocation,
PSO/device, command-list or fence implementation is added. This bounds
W0(c)'s legacy progress to the two named compute workloads: other bindings,
CBVs, graphics stages and the wider legacy instruction corpus still need
coverage. W0(c)/W0(g)/W1 remain open.

### W0(g) public GPU VA reservation boundary (VM)

The 2026-11-18 follow-up tests a different public runtime-owned callback
boundary, not another separate-device allocation. The default-off
`FEME_PROBE_GPU_VA` variant calls
`pKTCallbacks->pfnReserveGpuVirtualAddressCb(hRTDevice.handle, ...)`
inside owned `CreateDevice`, requesting 65,536 bytes of **unmapped address
space**, not memory. It uses zero `hAdapter` so the runtime supplies its own
kernel adapter handle, zero base/minimum/maximum addresses so VidMm chooses
the range, and zero reserved fields. It neither supplies an obsolete paging
queue handle nor interprets a GPU address as a CPU pointer.

The public
[reserve callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_reservegpuvirtualaddresscb),
[reservation structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_reservegpuvirtualaddress)
and
[free callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_freegpuvirtualaddresscb)
define the experiment. Only exact `S_OK` establishes reservation ownership.
A nonzero returned address is checked for 64 KiB alignment and range
overflow, then freed with the same size, including malformed successful
outputs. A failed reservation is never freed; its raw address/result are
retained in telemetry. Callback exceptions, non-`S_OK` success statuses,
missing outputs and failed cleanup cannot satisfy the success gate.

**Measured result: reservation is rejected, not a working reserve/free
lifecycle.** With SDK/WDK 10.0.26100.0 on the existing Windows 11 ARM64 VM,
all three configurations below returned `E_INVALIDARG` (`0x80070057`) at
DDIs 0010, 0022, 0072 and 0110:

| Runtime configuration | Reserve result | Address / free |
|---|---|---|
| x64 inbox, emulated on ARM64 | `E_INVALIDARG` | Zero / not attempted |
| Native ARM64 inbox | `E_INVALIDARG` | Zero / not attempted |
| Native ARM64 Agility 619 | `E_INVALIDARG` | Zero / not attempted |

The entry counters are `Open=1 Size=1 Versions=2 Caps=1 Optional=0 Fill=0
Create=1 Close=1 Destroy=0`; negotiated DDI and owned-module identity still
match exactly. The callback returns normally without an exception. Its
failure propagates to `D3D12CreateDevice`, rather than being replaced with
the usual `E_NOTIMPL`. DDIs 0070/0071 still reject before `CreateDevice` and
never attempt this probe. Agility's recorded core path is executable-local
`AgilitySDK\D3D12Core.dll`; no reference forwarding module is loaded.

A separate, one-off native public KMT diagnostic opened the same asserted
WARP LUID (`00000000:0000875d`) through `D3DKMTOpenAdapterFromLuid`.
`D3DKMTQueryAdapterInfo(KMTQAITYPE_WDDM_2_0_CAPS)` returned exact success
and `Value=0x5`, `GpuMmuSupported=0`, `IoMmuSupported=1`.
`D3DKMTReserveGpuVirtualAddress` with that adapter, size 65,536 and otherwise
zero inputs returned `STATUS_INVALID_PARAMETER` (`0xc000000d`), address
zero; adapter close succeeded. This diagnostic is not an automated control
or a runtime-device bridge. The public
[GpuMmu and IoMmu models](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-memory-in-wddm-2-0)
distinguish separate GPU page-table management from shared CPU address
space. These observations support investigating the selected WARP
CPU-memory contract rather than assuming GpuMmu reservations are a
prerequisite. They do not prove the exact internal cause of rejection or
that a public runtime-owned allocation path is impossible.

The runner has two deliberately different strict expectations:

- `--expect-gpu-va` requires exact successful reserve/free, a valid range
  and the usual deliberate `E_NOTIMPL` device result. It **fails** on every
  measured configuration above.
- `--expect-gpu-va-rejected` requires enabled/attempted/returned reservation,
  exact `E_INVALIDARG`, zero address, no free attempt and no exception,
  alongside exact device-result, negotiation, module and cleanup checks.
  This is a negative boundary measurement, not a resource pass.

`FEME_PROBE_GPU_VA_EXPECT_INVALIDARG=ON` explicitly selects the latter
expectation in the matrix; it is not an automatic fallback. It requires
`FEME_PROBE_GPU_VA=ON`. GPU VA and paging-queue probes are mutually
exclusive, and both require an explicit DDI matrix.

LIT validation, Debug assertions, disabled PCH and `/W4 /WX`:

- The six-DDI exact-rejection matrix passed 20/20 on each of x64 inbox,
  native inbox and native Agility, each repeated three times. These counts
  include wrong-stage, disabled-paging and wrong-GPU-VA-outcome controls;
  they do not represent successful reservations.
- Native and x64 bootstrap suites each passed 31/31: the direct adapter
  regression, 14 existing paging fixtures and 16 new GPU VA fixtures.
  New fixtures cover reserve/free success and failure, missing
  device/table/callbacks, zero/misaligned/overflowing addresses, exceptions,
  unexpected positive statuses, and `E_INVALIDARG` with zero or invalid
  nonzero output. Successful reservation/free remains mock-only evidence.
- Reconfiguration to default-off passed 24/24 x64 negotiation/disabled
  controls; switching to paging passed 20/20, without stale GPU VA tests.
- All 15 native Agility reference-WARP/buffer-copy controls still pass.

To reproduce the negative boundary matrix, use the matching developer
environment and Python with LIT timeout support, as above:

```powershell
$repo = 'C:\Users\cbieneman\dev\llvm-project\feme'
$build = 'C:\Users\cbieneman\dev\warp-gpu-va-probe'
$versions = @('0010', '0022', '0070', '0071', '0072', '0110')
$ddi = ($versions | ForEach-Object { "D3D12DDI_SUPPORTED_$_" }) -join ';'
cmake -S "$repo\utils\windows-warp-interposition" -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe `
  -DCMAKE_SYSTEM_VERSION=10.0.26100.0 `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1" `
  -DFEME_PROBE_DDI_MATRIX_ONLY=ON -DFEME_PROBE_GPU_VA=ON `
  -DFEME_PROBE_GPU_VA_EXPECT_INVALIDARG=ON `
  "-DFEME_PROBE_DDI_VERSIONS=$ddi" `
  "-DFEME_PROBE_DDI_EXPECT_UNSUPPORTED=D3D12DDI_SUPPORTED_0070;D3D12DDI_SUPPORTED_0071"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

For the measured native Agility matrix, use a separate native build and
add `-DCMAKE_CXX_FLAGS=/DFEME_PROBE_AGILITY_VERSION=619` when configuring.
Build the executables first, then stage the matching ARM64 core before
running LIT:

```powershell
cmake --build $build --parallel 2
foreach ($version in $versions) {
  $dir = "$build\pinned-version-custom-$version\AgilitySDK"
  New-Item -ItemType Directory -Force $dir | Out-Null
  Copy-Item C:\Users\cbieneman\dev\llvm-project\build-rel\bin\D3D12\D3D12Core.dll $dir
}
cmake --build $build --target check-feme-windows-probes --parallel 2
```

Keep W0(g) open: no real owned device, runtime-owned allocation,
residency or mapped-resource lifecycle follows from this rejection.
No private WARP ABI, system binary replacement or kernel-driver change
was used.

### W0(g) runtime-owned monitored-fence CPU lifecycle (VM)

The next 2026-11-18 follow-up proves an independent synchronization service
through the runtime-owned KT device callbacks, without creating a separate
KMT device, submitting GPU commands or supplying private WARP data.
It does not bypass the unresolved allocation contract. In particular, the
public [IoMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iommu-model)
still requires the UMD to allocate through VidMm for residency, resource
sharing and primary-surface visibility. Shared CPU/GPU address space is not
permission to substitute a bare `VirtualAlloc` region for that contract.
The inspected legacy and 0022 allocation callback structures still have no
documented size/standard-allocation input bridging the earlier KMT control.

The default-off `FEME_PROBE_MONITORED_FENCE` variant:

1. Requires the real runtime device handle and all four public KT
   create/destroy/CPU-wait/CPU-signal callbacks before doing any work.
2. Calls `pfnCreateSynchronizationObject2Cb` with
   `D3DDDI_MONITORED_FENCE`, initial value zero, engine affinity 1 and
   otherwise-zero flags/inputs. A nonzero sync handle and an aligned
   read-only CPU mapping are required. No GPU address is dereferenced.
3. Creates a fresh nonsignaled auto-reset event, requires the initial mapped
   value to be zero, then registers an asynchronous
   `pfnWaitForSynchronizationObjectFromCpuCb` wait for fence value 7.
4. Requires an immediate event poll to return `WAIT_TIMEOUT`: the wait
   must actually be pending before signaling, not an already-satisfied
   wait mistaken for progress.
5. Calls `pfnSignalSynchronizationObjectFromCpuCb` with value 7. Waits up to
   10,000 ms for the event and requires the mapped fence value to equal 7.
   The probe only reads the mapping; it never writes the read-only page.
6. Closes the completed event and destroys the sync object through
   `pfnDestroySynchronizationObjectCb`. Every successfully created object
   with a valid handle is offered to destroy, including incomplete
   mapping/initial-value and later wait/signal failure cases.

Only exact `S_OK` is accepted from each callback. Exceptions, early wakeup,
timeout, incorrect mapped values, missing outputs and failed cleanup
cannot satisfy `--expect-monitored-fence`. If an attempted asynchronous
wait may still be registered, the failure path explicitly retains its event
until process exit rather than closing a potentially live registration.
The telemetry records each attempted/returned stage, HRESULT, mapped
values, pending/completed state, retained event, exception and Win32 error.
See the public
[sync object structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddi_synchronizationobjectinfo2)
and [CPU wait callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_waitforsynchronizationobjectfromcpucb).

Measured results with SDK/WDK 10.0.26100.0 on the existing ARM64 VM:

| Runtime configuration | DDIs 0010 / 0022 / 0072 / 0110 |
|---|---|
| x64 inbox, emulated on ARM64 | All four callbacks `S_OK`; pending wait, completion and mapped 0 -> 7 verified |
| Native ARM64 inbox | Same |
| Native ARM64 Agility 619 | Same; executable-local `AgilitySDK\D3D12Core.dll` verified |

No event is retained on success. Entry counters remain
`Open=1 Size=1 Versions=2 Caps=1 Optional=0 Fill=0 Create=1 Close=1 Destroy=0`,
with exact pinned DDI, WARP identity and owned-module provenance checks.
The **device** result remains deliberate `E_NOTIMPL` after successful sync
cleanup; the object-destroy callback above is not `DestroyDevice`.
DDIs 0070/0071 still return `DXGI_ERROR_UNSUPPORTED` before device creation,
with no monitored-fence attempt. No reference forwarding module is loaded.

LIT-only validation, Debug assertions, disabled PCH and `/W4 /WX`:

- The six-DDI matrix passed 24/24 on x64 inbox, native inbox and native
  Agility, each repeated three times. This includes wrong-stage and
  disabled paging/GPU-VA expectation controls.
- Native and x64 bootstrap suites each passed 57/57: 26 new monitored-fence
  scenarios plus the existing 31 controls. New cases cover successful
  lifecycle, missing callbacks/device/table/outputs, misaligned mapping,
  incorrect initial/completed values, early event, wait timeout, and
  failure, unexpected positive status and exception at each callback.
  The timeout fixture exercises the actual 10-second bound.
- Reconfiguration of the x64 matrix passed default-off negotiation and
  disabled-probe controls (28/28), paging (24/24), and exact GPU VA
  rejection (24/24), without stale generated tests.
- All 15 native Agility reference adapter/buffer-copy cases still pass.

To reproduce, use the matching developer environment and Python with LIT
timeout support:

```powershell
$repo = 'C:\Users\cbieneman\dev\llvm-project\feme'
$build = 'C:\Users\cbieneman\dev\warp-monitored-fence-probe'
$versions = @('0010', '0022', '0070', '0071', '0072', '0110')
$ddi = ($versions | ForEach-Object { "D3D12DDI_SUPPORTED_$_" }) -join ';'
cmake -S "$repo\utils\windows-warp-interposition" -B $build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=cl.exe `
  -DCMAKE_SYSTEM_VERSION=10.0.26100.0 `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1" `
  -DFEME_PROBE_DDI_MATRIX_ONLY=ON -DFEME_PROBE_MONITORED_FENCE=ON `
  "-DFEME_PROBE_DDI_VERSIONS=$ddi" `
  "-DFEME_PROBE_DDI_EXPECT_UNSUPPORTED=D3D12DDI_SUPPORTED_0070;D3D12DDI_SUPPORTED_0071"
cmake --build $build --target check-feme-windows-probes --parallel 2
```

For native Agility, use the same separate-build compiler definition and
matching-core staging procedure as the GPU VA section above. The
standalone `windows-adapter-bootstrap` project runs the 57 fixture/direct
controls through `check-feme-windows-probes`. Monitored-fence probing
requires explicit DDI versions and cannot be combined with paging or
GPU VA probing in one variant; other variants remain default-off.

This establishes a live public runtime-owned CPU fence service, not an
`ID3D12Fence` implementation, GPU queue execution, runtime-owned allocation/
residency or a real FeMe D3D12 device. W0(g) and W1(a) remain open.

### Reproduce the B investigation

In the Windows development VM's x64 developer shell, use an SDK/WDK with the
required public DDI macros, CMake >= 3.29 and the existing `lld-link` on PATH.
The previously tested forwarding build used SDK/WDK 28000.
`CMAKE_LINKER_TYPE=LLD` selected the linker; setting `CMAKE_LINKER` alone did
not.
MSVC-link forwarding attempts failed with unresolved exports; their exact
cause was not established.

```powershell
cmake -S C:\dev\llvm-project\feme\utils\windows-warp-interposition `
  -B C:\dev\warp-interposition -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_LINKER_TYPE=LLD `
  "-DCMAKE_CXX_FLAGS_DEBUG=/Z7 /Ob0 /Od /RTC1" `
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache `
  -DFEME_WARP_REFERENCE=C:\dev\llvm-project\build\bin\d3d10warp.dll
cmake --build C:\dev\warp-interposition
cmake --build C:\dev\warp-interposition --target check-feme-windows-probes
```

Use the VM's paths and toolchain. A previous host required a typed
manifest-tool override (`-DCMAKE_MT:FILEPATH=...`), described below; do not
carry that workaround into the VM unless its compiler check reproduces the
same manifest-tool launch failure. Explicitly select the MSVC compiler if
prepending LLVM's directory to PATH, so CMake does not accidentally choose
GNU-style clang with MSVC-only flags.

For Agility, build `windows-adapter-probe` separately with the existing
`FEME_PROBE_AGILITY_CORE`/`FEME_PROBE_AGILITY_VERSION=619` options below,
then run that executable with `--warp-shim <bare DLL>`,
`--warp-bootstrap <negotiation DLL>` and `--stub <bare DLL>`.
The harness's explicit load permits those DLLs to reside in the other build's
directories. Check the reported AgilitySDK core path and reference-module
absence; an Agility build option alone is not runtime-selection evidence.

### Windows development VM starting point

Use the VM's x64 developer shell, selected SDK/WDK, CMake, Ninja, and sccache.
The historical container had no `CMAKE_MT` issue; on the VM use its matching
manifest tool, and only select a different `CMAKE_MT` path if the configured
tool cannot launch.

```powershell
cmake -S C:\dev\llvm-project\feme\utils\windows-adapter-probe `
  -B C:\dev\adapter-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache `
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
cmake --build C:\dev\adapter-probe
cmake --build C:\dev\adapter-probe --target check-feme-windows-probes
```

The four cases are reference WARP, software-reference WARP, pre-DDI stub
rejection, and explicit missing-module failure. Historical container results
for these cases are recorded above; run them again in the VM for current
validation. For the Agility-core matrix rows, reconfigure into a second build
directory with `-DFEME_PROBE_AGILITY_VERSION=619` and
`-DFEME_PROBE_AGILITY_CORE=<path to a VM-local D3D12Core.dll>`, then
run that build's four LIT tests and `feme-windows-adapter-probe.exe` directly
with no arguments, `--software <WARP path>`,
or `--stub <path to feme-adapter-rejection-stub.dll>`.

## Historical host probe workaround

This command records the workaround used on the earlier host, not the current
VM setup. Start with the VM's matching SDK/WDK manifest tool; use the
`CMAKE_MT` override only if the same launch failure occurs there. Choose an
out-of-tree build directory; the standalone project has no FeMe/LLVM library
dependency.

```powershell
cmake -S C:\Users\cbieneman\dev\llvm-project\feme\utils\windows-adapter-probe `
  -B C:\dev\adapter-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER_LAUNCHER=C:\Users\cbieneman\.cargo\bin\sccache.exe `
  "-DCMAKE_MT=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe"
cmake --build C:\dev\adapter-probe
cmake --build C:\dev\adapter-probe --target check-feme-windows-probes
C:\dev\adapter-probe\feme-windows-adapter-probe.exe --software `
  C:\Users\cbieneman\dev\offload-test-suite\build\bin\d3d10warp.dll
```

For a separate Agility build, also supply:

```powershell
-DFEME_PROBE_AGILITY_VERSION=619 `
-DFEME_PROBE_AGILITY_CORE=C:\Users\cbieneman\dev\offload-test-suite\build\agility-sdk\build\native\bin\x64\D3D12Core.dll
```

The project copies only the explicitly supplied, existing Microsoft core into
that executable's `AgilitySDK` subdirectory. It does not download dependencies
or add binaries to the source tree. Repeat `--software` with an inbox or NuGet
WARP path to vary WARP independently of the D3D12 core.

On this host, SDK 28000 `mt.exe` fails to launch with `0xc0000135` during
CMake's compiler check. Its import table includes `MidlrtMd.dll`, which is
absent from the checked x64/x86 SDK 28000 tool directories. Selecting the
existing SDK 26100 manifest tool fixes that check while retaining SDK/WDK
28000 headers, libraries and resource compiler. Selecting SDK 26100 wholesale
is not equivalent: the required DDI headers are absent in that kit here.
This is a local probe-build workaround, not a change to the supported product
kit pairing or an instruction to mix kits for a production driver build.
