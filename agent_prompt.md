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

Please begin work on the WARP roadmap. Suggested next steps:

**Suggested next steps (ranked)**:
1. **Continue W0(g)'s container-first public-DDI investigation from the
   completed W0(h) evidence.** The user authorized bounded investigation of B;
   do not ask that research question again or treat all D3D12 work as blocked.
   Read `feme/docs/W0WindowsIntegrationProbe.md`'s alternative-B section.
   Source-built host/container inbox and Agility 619 tests route stock D3D12
   into owned `OpenAdapter12`, query its public adapter table and close it
   without ordinal 199, reference forwarding or a new/custom kernel driver.
   Reproduce the six standalone interposition CTests, then audit the smallest
   practical supported public DDI version and required adapter/device/thunk
   services. Separate missing owned implementation from any genuinely required
   private boundary. The current fixture advertises zero versions and creates
   no device; Microsoft's WARP identity is a diagnostic limitation, not FeMe
   selection or shipping approval. Stop for explicit W0(b) sign-off before
   private ABI implementation or changing product identity/distribution.
   Do not install drivers, alter system settings, or switch to a COM runtime.
   Standalone diagnostics need targeted native tests; compiler/Vulkan changes
   still need the existing check-feme/CTS gates.
2. **W0(c)'s remaining legacy-shader-contract audit.** Candidate-file triage
   is already complete: nine files are actual DXBC inputs and `pch.hpp` is
   merely adjacent. Do not repeat it. Determine what the pinned stock-runtime
   contract requires from FeMe's existing DXBC importer and later execution
   path; do not treat classification or import support as passing shaders.
3. Backlog carried from many prior sessions, still untouched:
   `ENV6(c)(3)`/`ENV6(d)` CTS triage, `ENV7(g)` audit, full Vulkan CTS
   re-measure.
