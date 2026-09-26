---
model: claude-sonnet-5
resume: ed0156d8-7270-4d71-9053-e6cff6d7f6b5
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

# Request

The previous session ended without completing any work. Any intermediate
products are stashed and may be restored with `git stash pop`.

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **L205** (chain-with-phi branch generalization in `EntryWrapper.cpp`,
   50 cases, `memory_model.shared.16bit.*`): fully scoped by a prior
   session (see roadmap row), not started. Estimate: half a day to a
   full day -- three coordinated changes to `BranchShape`/
   `matchBranchShape`/`buildWrapperForBranch`. Highest-value remaining
   item with a clear scope.
2. **L201(d)** (mesh/tessellation f16 I/O correctness, 120 cases):
   "Result does not match reference," no pixel detail yet. Needs
   `--deqp-log-images=enable` or a hand-built repro first. Estimate: 1
   hour just to get a first real diagnostic, unknown after that.
3. **L201(b)/(c)** (27 + 25 cases): reduced to distinct symptoms two
   sessions ago, neither started. Lower case count than (d)/L205.
4. `input_output_float_32_to_16`'s own 100 `_rtz`-rounding-mode failures
   (noted, not investigated, two sessions running now): worth a 10-minute
   look to confirm it isn't already tracked before opening a new row.
5. A full/broad CTS re-run is overdue (last one: 2026-09-26). This
   session only reran `16bit_storage.*` (2431 cases), not the full
   48,307-case list. Once L205 closes, that's a natural trigger point --
   or do it now, since two fix rounds (L202, L206) have landed since the
   last full run and might have surfaced more incidental passes.
6. Not yet done this session: `check-hlsl-feme-vk` against the
   offload-test-suite `feme` branch (`/home/dev/dev/offload-test-suite`),
   carried over unstarted from two sessions ago. Estimate: 15-30 minutes
   if the branch still builds cleanly -- worth doing before the next
   dxc-shape-sensitive fix, not just after.
