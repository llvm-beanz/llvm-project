---
model: claude-sonnet-5
resume: db99d377-bdef-4dd8-a2b5-787bcf66bc23
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~2-4 hours, standing, several sessions old)** `L411`'s real residual bug:
   rasterization/subpixel-coverage precision in the 66-case
   `b10g11r11_ufloat_pack32` dual-source-blend family. Needs a dedicated session
   comparing per-row coverage/edge-function values near x=12/x=20, not blend
   arithmetic.
2. **(~1-2 hours)** `L414(a)`'s `standardSampleLocations` item — needs the
   Vulkan-spec-standard multisample sample-location pattern implemented in the
   rasterizer, not a reported-bit flip.
3. **(~1-2 hours)** `L414(a)`'s `timestampComputeAndGraphics` item — needs real
   compute-queue timestamp-query support (currently graphics-queue only). Check
   `QueryPool.cpp`'s `vkCmdWriteTimestamp` queue-family gating first.
4. **(~3-6 hours, real architecture work, not a quick win)** `L236`: add
   per-device `EnabledExtensions` tracking plus a command-to-owning-extension(s)
   map so `vkGetDeviceProcAddr` can correctly return null for a disabled
   extension's functions. This is now the single remaining item blocking a clean
   `api.version_check.entry_points`/`api.get_device_proc_addr.non_enabled` pair.
5. **(~1-2 hours, lower priority, no CTS-visible symptom found yet)**
   `entry_points`'s 8 genuinely-unimplemented functions (sparse residency, YCbCr
   conversion, buffer device address) — worth a feasibility check per function
   before committing to implement any of them; none has an assigned roadmap item
   yet.
