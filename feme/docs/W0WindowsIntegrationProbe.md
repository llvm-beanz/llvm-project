# W0 Windows user-mode integration probe

**Next step: `W0(b)`'s decision gate is now triggered. No public, no-install,
third-party route to `OpenAdapter12` was found -- see
[Decision: no public third-party route to `OpenAdapter12` was found](#decision-no-public-third-party-route-to-openadapter12-was-found).
An explicit approval is needed among an installed-adapter (kernel-mode
driver) alternative, a private-compatibility scope change, or an
application-local COM runtime before further `W0(g)` prototype work.**

This is host and container evidence for [WARPRoadmap.md](WARPRoadmap.md),
collected on 2026-11-06. The user selected stock-runtime user-mode/software-adapter probing
first, with no system changes. The LLVM-owned
[standalone probe](../utils/windows-adapter-probe/CMakeLists.txt) uses public
SDK/WDK types and APIs. It does not contain Microsoft implementation code,
invoke private WARP APIs, install an adapter, or execute FeMe shaders.

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
| FeMe D3D12 DDI negotiation | Not reached; no negotiated FeMe DDI version or valid device tables |
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

CTest has four cases: `reference-warp`, `software-reference-warp`,
`rejection-stub`, and `missing-module`. All four passed against each runtime.
The negative stub case requires successful adapter creation/interface query
followed by specifically `DXGI_ERROR_UNSUPPORTED`; an arbitrary earlier failure
is not accepted as a successful experiment.

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

## Remaining work and decision gate

**Direction selected after the host probe: develop the application-local
user-mode software driver in the Windows container using stock D3D12/DXGI.**
This selects implementation order and the first integration boundary to prove,
not a claim that the third-party contract or container runtime already works.
It does not select a replacement COM runtime or require a kernel installation.

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
   No such contract was found; this triggers item 4 below.
3. Extend an LLVM-owned prototype through open/callback negotiation/close with a
   distinct FeMe identity. Measure the chosen DDI version and teardown, including
   failure cleanup, rather than inferring support from the installed headers.
   The [direct-call experiment](#direct-openadapter12-invocation-bypassing-dxgi)
   shows the entry point's own mechanics are callable; item 2's negative
   result means this step cannot proceed through a third-party, no-install
   `OpenAdapter12` route without first resolving item 4.
4. If only WARP-private binary compatibility makes the application-local UMD
   route work, request an explicit product/design decision. Do not silently
   impersonate Microsoft's adapter, use ordinal 199, or declare an installed
   miniport mandatory. The alternatives are an approved compatibility project,
   the separately installed WDDM path, or the application-local COM runtime.
   **This item is now reached**: no public contract was found (item 2), so an
   explicit decision among these alternatives is needed before further
   prototype work.

W0(a), W0(b), and W0(d) remain incomplete: there is no FeMe D3D12 device, and
no installed-path test. The user-mode stock-runtime direction is selected and
now container-verified at the measurement level, but no public bootstrap
route to `OpenAdapter12` was found, so `W0(b)`'s decision gate is now
triggered rather than closed. The reference-test portion of W0(d) has host and
now container evidence only. Installed-driver, signing/reboot, display and
HLK work are separate host/VM tasks if that deployment requires them; they do
not block continuing W0(f)'s bootstrap step (tracked as `W0(g)` in
`WARPRoadmap.md`). Missing evidence is not Pass.

## Container measurements

Collected 2026-11-06 in this process-isolated Windows container, using the
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
| FeMe D3D12 DDI negotiation | Not reached in this container either; no negotiated FeMe DDI version or valid device tables |

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
occurred in the container either. This is still not FeMe DDI negotiation --
the rejection stub's `OpenAdapter12` counter stays at zero in every run,
confirming the stub (and, by the same loader mechanics, any future FeMe
module without a real DDI implementation) is rejected before reaching that
negotiation, not evidence that negotiation itself would succeed.

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
involvement -- the missing piece is exclusively the OS-side routing from a
third-party module to that entry point, not the entry point's own mechanics.

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
   specifically that nothing in the public DXGI/D3D12/DXCore surface routes a
   third-party module's `OpenAdapter12` the way `CreateSoftwareAdapter`
   routes to `OpenAdapter10`/`OpenAdapter10_2`.
4. The reference WARP export table's private ordinal-199 `WarpPrivateApi`
   (noted in the host measurements above) is consistent with Microsoft's own
   WARP being loaded through an internal path specific to WARP's identity,
   not through the public `CreateSoftwareAdapter` contract either -- further
   evidence (not proof, since no Windows source was read) that no general
   public bootstrap contract for third-party D3D12 software adapters exists
   today.

**No public, third-party, no-install bootstrap contract to `OpenAdapter12`
was found.** This answers `W0(g)`'s "identify a public third-party adapter
identity/thunk bootstrap" step with a negative result rather than a positive
one. Per the remaining-work list above, this **triggers `W0(b)`'s decision
gate**: an explicit approval is needed among (a) an installed-adapter
alternative (kernel-mode driver, even a minimal/stub one, per this session's
web research), (b) a private-compatibility scope change (depending on WARP's
own private loading path, which the design document already prohibits
without separate approval), or (c) an application-local COM runtime (with its
own documented added DXGI/debug-layer/DRED/distribution cost). This document
does not select an alternative; it records that the gate is now reached and
recommends evaluating option (a) first, since it is the only one not already
flagged as requiring a separate design exception.

### `W0(b)` alternative cost/scope evaluation (not a selection)

Collected 2026-11-07, from Microsoft's public driver-signing documentation
and the WARP architecture documented by Microsoft's own public articles. This
section only documents cost/scope for each alternative so a future session or
maintainer can make the `W0(b)` decision with concrete information; it does
**not** select an alternative.

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

**(b) Private-compatibility scope change, cost.** This means depending on
WARP's own undocumented loading path -- consistent with the reference WARP
module's private ordinal-199 `WarpPrivateApi` export noted in the host
measurements above -- rather than any publicly documented contract. Its cost
is not engineering effort but risk and process: the design document already
prohibits relying on undocumented Microsoft internals without a separate
design exception, because (1) private ordinals carry no compatibility
guarantee and can change or disappear in any Windows/Agility SDK update with
no notice, (2) reverse-engineering a private Microsoft API surface raises
legal/compliance questions distinct from the purely public-API-based
engineering work done so far in this probe, and (3) FeMe's own measurements
already show the DXGI/D3D12 runtime special-cases WARP's enumeration
(`IDXGIFactory4::EnumWarpAdapter`, per Microsoft's public
[WARP guide](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/directx-warp))
rather than routing it through the generic third-party adapter path explored
in `W0(g)` -- so this alternative would mean impersonating or hooking that
WARP-specific special case, not extending a general-purpose contract. This
alternative's cost cannot be bounded by further research alone; it requires
the separate design sign-off the existing prohibition already calls for.

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

### Container starting point

Use the Windows tooling's x64 developer-shell setup and its existing sccache.
The container's actual kit selection needs no `CMAKE_MT` workaround (see the
measurements above); this differs from the host's SDK 28000 `mt.exe` failure.

```powershell
cmake -S C:\dev\llvm-project\feme\utils\windows-adapter-probe `
  -B C:\dev\adapter-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache `
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
cmake --build C:\dev\adapter-probe
ctest --test-dir C:\dev\adapter-probe -V
```

The four cases are reference WARP, software-reference WARP, pre-DDI stub
rejection, and explicit missing-module failure; all four passed as recorded
above. For the Agility-core matrix rows, reconfigure into a second build
directory with `-DFEME_PROBE_AGILITY_VERSION=619` and
`-DFEME_PROBE_AGILITY_CORE=<path to a container-local D3D12Core.dll>`, then
run `feme-windows-adapter-probe.exe` directly (not via `ctest`, which only
exercises the inbox-core build) with no arguments, `--software <WARP path>`,
or `--stub <path to feme-adapter-rejection-stub.dll>`.

## Reproduce without configuring the LLVM build

Use a VS x64 developer shell with SDK/WDK `10.0.28000.0`. Choose an out-of-tree
build directory; the standalone project has no FeMe/LLVM library dependency.

```powershell
cmake -S C:\Users\cbieneman\dev\llvm-project\feme\utils\windows-adapter-probe `
  -B C:\dev\adapter-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER_LAUNCHER=C:\Users\cbieneman\.cargo\bin\sccache.exe `
  "-DCMAKE_MT=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe"
cmake --build C:\dev\adapter-probe
ctest --test-dir C:\dev\adapter-probe -V
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
