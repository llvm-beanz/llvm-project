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

1. **(ongoing, whenever it finishes or a future session wants to check in)**
   Check the detached `dEQP-VK.pipeline.*.blend.*` sweep (`ps aux | grep
   deqp-vk`, tally `/tmp/l411_blend_full_sweep.{log,qpa}`); fold into
   `VulkanCTSReport.md`, clean up the temp files once done.
2. **(~multi-session, real architecture work, now with a corrected scoping
   writeup to start from)** `H46`: implement the sparse-resources binding-range
   data model per this session's scoping findings
   (`Buffer.h`/`Image.h`/`Memory.h`, ~179 call sites to audit). Start with the
   new data structure and a zero-behavior-change migration of the
   always-fully-bound case, proven via unit tests, before touching feature bits
   or any of the 179 call sites' actual logic.
3. **(~1-2 hours, standing, recommend doing a *real* pass not just a quick
   grep)** Keep doing roadmap hygiene passes periodically — this session's
   deeper read (checking each open row's own dependency chain, not just
   milestone-ID nesting) found real issues a shallower pass missed. Worth
   repeating with the same depth, not reverting to the lighter check.
4. **(opportunistic, unchanged)** If a future session is in
   `Buffer.cpp`/`Image.cpp`/`Sync.cpp` for `H46` or any other reason, the new
   forward-pointer comment in `Sync.cpp`'s `vkQueueBindSparse` flags exactly
   what needs to change there once sparse resources become real.

