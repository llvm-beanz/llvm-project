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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

**Always**: Make sure you are not using precompiled headers in your build.

**Always**: End each session with a commit of the agent thoughts even if
mid-investigation to avoid losing documentation.

# Request

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~1-2 days, highest leverage available)** `L432(a)`: implement the
   `Linearize.cpp` fix. Start with a minimal, committed `.ll` regression
   test from `/tmp/sync_op_prenorm.ll`'s shape (regenerable via
   `FEME_DUMP_IR_PRENORM=1` + the `write_blit_image_read_image_compute.
   image_128_r32_uint_concurrent` repro if `/tmp` has been cleared). Then
   extend the check-recovery logic to (a) walk forward from `Header`
   through trivial unconditional pass-through blocks to find a real check,
   and (b) recognize a *uniform* phi-merged `Flow`-style dispatch block on
   the exit side, not just a divergent one. Verify via
   `FeMeTransformsCPUTests`/`check-feme`, then a real CTS re-run of
   `synchronization.op` (expect most of the 9,795 to flip to Pass/
   NotSupported).
2. **(standing)** `Basic/Mandelbrot.test`'s flake -- confirmed still fixed
   this session (492/492), not re-broken, but still not actively worked on
   as its own item in several sessions now.
3. **(standing)** `offload-test-suite` `feme`-branch-vs-`main` drift
   re-check -- not re-verified this session, no `offload-test-suite` work
   was done and the checkout was not re-cloned, so drift risk is unchanged.
