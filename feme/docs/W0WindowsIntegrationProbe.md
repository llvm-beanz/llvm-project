# W0 Windows user-mode integration probe

**Bounded alternative-B investigation authorized and routing proved.**
Stock D3D12 reaches LLVM-owned `OpenAdapter12`, queries its public adapter
callbacks, and closes it on both the host and Windows container. The owned
cases need neither ordinal 199 nor reference-WARP forwarding. This uses
Microsoft's existing WARP adapter identity, not a distinct FeMe adapter;
no supported DDI version or FeMe device exists yet. See
[Alternative B: application-local WARP DDI routing](#alternative-b-application-local-warp-ddi-routing).
The public general-provider search remains negative, but that is not a
runtime-routing impossibility result. `W0(b)` remains a product/compatibility
decision, not a reason to repeat the already-authorized research question.

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
   proves the entry point's mechanics; alternative B below now proves
   stock-runtime routing and adapter queries/close without a new kernel driver.
   Supported-version negotiation, device creation and distinct identity remain
   separate unfinished requirements.
4. If only WARP-private binary compatibility makes the application-local UMD
   route work, request an explicit product/design decision. Do not silently
   impersonate Microsoft's adapter, use ordinal 199, or declare an installed
   miniport mandatory. The alternatives are an approved compatibility project,
   the separately installed WDDM path, or the application-local COM runtime.
   The user explicitly authorized the bounded B investigation recorded below.
   That permits controlled WARP-identity diagnostics, not private-ABI guessing,
   product identity changes, or automatic adoption of a shipping architecture.

W0(a), W0(b), and W0(d) remain incomplete: there is no FeMe D3D12 device, and
no installed-path test. The user-mode stock-runtime direction is selected and
now container-verified through owned adapter queries/close in alternative B.
No public general-provider bootstrap was found and `W0(b)`'s shipping boundary
is not closed, but the research routing blocker is resolved. W0(d) has host and
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
runtime. The user subsequently authorized investigation of B. Its successful
result below supersedes the blanket prototype-routing blocker and the earlier
recommendation to evaluate A first; it does not select B for distribution.

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

**(b) WARP-specific compatibility, cost.** Separate two risks: relying on
application-local WARP filename/identity selection as a provider boundary,
and depending on actual private binary APIs. The bounded investigation below
uses the first but does not need the second to enter owned adapter callbacks.
It uses the public `EnumWarpAdapter` API and public WDK DDI signatures; it
does not hook APIs or guess ordinal-199 signatures. The filename/identity
route is still not a documented general third-party registration contract,
and the resulting Microsoft adapter identity is unsuitable as a truthful
FeMe product identity. Any later private-API requirement adds compatibility
and licensing-review costs; ordinals can change without a compatibility
guarantee. Shipping adoption therefore still needs a separate design decision.

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
The approval covers bounded, application-local diagnostics, not installation,
system hooks, guessed private ABIs, or adoption as FeMe's shipping boundary.
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
on these measured runtimes. This still uses the existing OS WARP adapter
infrastructure; it does not prove zero kernel involvement. No distinct FeMe
identity, supported DDI version, actual FeMe D3D12 device, feature level,
shader execution, presentation, or shipping compatibility is established.
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

### Reproduce the B investigation

In the container's x64 developer shell with SDK/WDK 28000, use CMake >= 3.29
and the existing `lld-link` on PATH. The tested forwarding build uses
`CMAKE_LINKER_TYPE=LLD`; setting `CMAKE_LINKER` alone did not select it here.
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
ctest --test-dir C:\dev\warp-interposition -V --repeat until-fail:3
```

On the host, use the same flags with host paths and the typed manifest-tool
override `-DCMAKE_MT:FILEPATH=...` described below. Explicitly select the
MSVC compiler when prepending LLVM's directory to PATH, so CMake does not
accidentally choose GNU-style clang with MSVC-only flags.

For Agility, build `windows-adapter-probe` separately with the existing
`FEME_PROBE_AGILITY_CORE`/`FEME_PROBE_AGILITY_VERSION=619` options below,
then run that executable with `--warp-shim <bare DLL>`,
`--warp-bootstrap <negotiation DLL>` and `--stub <bare DLL>`.
The harness's explicit load permits those DLLs to reside in the other build's
directories. Check the reported AgilitySDK core path and reference-module
absence; an Agility build option alone is not runtime-selection evidence.

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
run that build's four CTests and `feme-windows-adapter-probe.exe` directly
with no arguments, `--software <WARP path>`,
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
