---
model: claude-sonnet-5
resume: b8ce7a07-6a6f-4205-864b-b09621346aaf
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

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(half day, carried forward)** `L440(c)`'s remaining 7 residuals, triage
   individually: `Bugs/UAV-Sequental-Consistency.yaml` or
   `Feature/WaveOps/WaveActiveAnyTrue.test` (fails with `feme-cpu-linearize:
   function 'main': divergent branch in '' has no reconvergence point` — a
   `Linearize.cpp` diagnostic, worth checking first whether it's the same class
   of issue `L419(ae)`'s tessellation work already explored) look like the next
   most tractable single-case picks.
   `Feature/ResourcesInStructs/res-of-matrix-in-struct.test` (SIGABRT) is still
   only flagged across 3+ prior sessions, never actually isolated — worth a
   dedicated triage-only pass to classify it before committing a full session to
   a fix.
2. **(1 day, design-heavy, carried forward many sessions, untouched again)**
   `L419(ae)`: tessellation edge0-vs-edge1 ULP mismatch — proven architecturally
   unfixable via a local patch; needs real cross-patch-adjacency design.
3. **(low priority, carried forward)** `feme-run` can't execute a DXIL shader
   using a resource (not groupshared) `InterlockedAdd`/atomic.
4. **(low priority, carried forward)** 9 older git stashes remain uninspected.
5. **(low priority, carried forward)** 38-vs-39 `check-hlsl-clang-feme-vk`
   baseline discrepancy from several sessions back, never root-caused.

**Build/test state**: `check-feme` 3791/3798 passed, 7 unsupported, 0
regressions (+1 new unit test). `check-hlsl-clang-feme-vk`: 8 → 7 failed, 0
regressions. `VulkanCTSReport.md`/`Roadmap.md` both updated this session; no
inventory update needed (conversion-pattern completeness fix, no new
feature/extension); no real Vulkan CTS sweep scoped (DXC-only reachable code
path, no CTS-detectable regression surface).
