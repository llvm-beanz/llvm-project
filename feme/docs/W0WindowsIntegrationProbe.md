# W0 Windows user-mode integration probe

**Next step: run the standalone reference/rejection probes inside the Windows
container, then establish a valid third-party adapter bootstrap before
implementing FeMe's D3D12 device tables. The selected direction is a user-mode
software driver through stock D3D12/DXGI, not kernel installation.**

This is host evidence for [WARPRoadmap.md](WARPRoadmap.md), collected on
2026-11-06. The user selected stock-runtime user-mode/software-adapter probing
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

1. Build this probe and run all four CTest cases inside the Windows container.
   Record OS/kit/runtime/core/WARP versions and loaded paths, identity/LUID,
   HRESULTs and DDI counters. Verify reference WARP first; if it fails, record
   and isolate the actual environment/runtime blocker rather than using the
   host results as a substitute or assuming a kernel driver is required.
2. Identify the public, modern third-party software-adapter bootstrap contract
   needed to provide valid identity/thunk services and reach `OpenAdapter12`.
   No kernel installation is needed to build or run the loading experiments.
3. Extend an LLVM-owned prototype through open/callback negotiation/close with a
   distinct FeMe identity. Measure the chosen DDI version and teardown, including
   failure cleanup, rather than inferring support from the installed headers.
4. If only WARP-private binary compatibility makes the application-local UMD
   route work, request an explicit product/design decision. Do not silently
   impersonate Microsoft's adapter, use ordinal 199, or declare an installed
   miniport mandatory. The alternatives are an approved compatibility project,
   the separately installed WDDM path, or the application-local COM runtime.

W0(a), W0(b), and W0(d) remain incomplete: there is no FeMe D3D12 device, no
container runtime proof or installed-path test. The user-mode stock-runtime
direction is selected, but its feasibility remains open. The reference-test
portion of W0(d) has host evidence only. Installed-driver, signing/reboot,
display and HLK work are separate host/VM tasks if that deployment requires
them; they do not block starting W0(f). Missing evidence is not Pass.

### Container starting point

Use the Windows tooling's x64 developer-shell setup and its existing sccache.
Keep the container's actual kit selection; do not apply the host-only
`CMAKE_MT` workaround unless the same failure is independently reproduced.

```powershell
cmake -S C:\dev\llvm-project\feme\utils\windows-adapter-probe `
  -B C:\dev\adapter-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER_LAUNCHER=sccache `
  -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON
cmake --build C:\dev\adapter-probe
ctest --test-dir C:\dev\adapter-probe -V
```

The four cases are reference WARP, software-reference WARP, pre-DDI stub
rejection, and explicit missing-module failure. Use existing container NuGet
WARP/Agility binaries for additional matrix runs; inspect their locations and
versions instead of reusing the host's user-profile paths. These commands are
the next action, not recorded container test results.

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
