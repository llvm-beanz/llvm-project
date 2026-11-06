---
model: claude-sonnet-5
resume: 86dca4d4-0d8b-4652-b37b-c8ab60c0ce73
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file, and the environment-wide
agent skills at /home/dev/.agents/skills.

When you build and test ensure that you are using object file caching, and
building with assertions enabled. Also build and test the `check-feme` target
ensuring that all the target dependencies are correctly setup so that the test
dependencies will build before running the tests.

When you deviate from the design document please update the design document.

Also please run the Vulkan CTS from the checkout under /home/dev/dev/VK-GL-CTS/
after each change and update the VulkanCTSReport.md. Please keep the
Vulkan14FeatureInventory and VulkanExtensionInventory up to date with each
change as well.

If the request is to complete a roadmap stage, if you complete it please strike
it through on the roadmap document, if you do not, please add entries to the
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

**Before doing anything else**: `vulkaninfo --summary | grep deviceName` and
confirm `FeMe CPU Vulkan Device`. Every session from now on, every time, not
just once at the start.

**When you find an issue outside FeMe**: Create an isolated reproducer, fix it,
and apply the fix in a commit that only touches files from outside the FeMe
subdirectory. Ensure that fixes to other LLVM sub-projects are self-contained
and tested.

**When you find an issue with environment or process**: Codify it in Feme's
.instructions.md file so future sessions don't lose it.

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Please begin work on the WARP roadmap.

The selected direction is **container-first, application-local user-mode
software-driver integration with stock Windows D3D12/DXGI**. This is not a
request to implement a replacement COM runtime or install a kernel driver.
Read `feme/docs/W0WindowsIntegrationProbe.md` and `feme/docs/WARPRoadmap.md`.
Host reference-WARP results are recorded; container results and a working
third-party FeMe adapter are not yet established.

## Suggested next steps (ranked)

1. **`W0(f)`**: build `feme/utils/windows-adapter-probe` in the Windows
   container and run its four CTest cases first. Record the actual OS,
   SDK/WDK, runtime/core/WARP paths and versions, HRESULTs and DDI counters.
   Verify reference WARP before extending the FeMe adapter bootstrap to reach
   `OpenAdapter12`, callback negotiation and teardown with a distinct identity.
   Preserve caching, assertions and disabled PCH. Do not assume host success
   proves container success, or wait for kernel installation to start.
2. Re-examine whether any part of `W0(c)`'s DXBC/DXIL-input classification
   can be sharpened without a host: the grep-confirmed candidate list in
   `W0DirectXTestCorpusClassification.md` (`BasicAPI.cpp`,
   `GBVExecuteIndirect.cpp`, `Library.cpp`, `MultiGPU.cpp`,
   `PipelineStateObject.cpp`, `Raytracing.cpp`, `ResourceBinding.cpp`,
   `ShaderRootSig.cpp`, `util.{cpp,h}`, `pch.hpp`) was left as "needs
   individual triage" rather than resolved one-by-one; a short, bounded
   follow-up could read each file and record which actually consume DXBC
   inputs versus merely reference DXBC-related constants/headers.
3. Backlog carried from prior sessions, still untouched: `ENV6(c)(3)`/
   `ENV6(d)` CTS triage, `ENV7(g)` audit, full CTS re-measure.

Keep driver installation/signing/reboot and display/HLK work as separate
host/VM tasks if the selected deployment eventually requires them. Do not
replace system DLLs, silently use private WARP ordinals, or switch to an
installed miniport/COM-runtime architecture without an explicit design
decision. If a container probe fails, record its exact failure and isolate
the runtime/environment issue rather than declaring all W0 work host-blocked.
