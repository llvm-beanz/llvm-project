---
model: claude-sonnet-5
resume: 1ebfe39e-5f48-4c14-810b-08a0301888a7
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

The last session stalled out, any intermediate results will be in the git stash
and may be restored with `git stash pop`.

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(background, in progress, just needs monitoring)** `L258`: the
   full `dEQP-VK.glsl.*` sweep (shellId `37` if still alive) was
   ~44% through (12,525/28,420) at session end, ~2 cases/sec. Next
   session: check if it finished, and if so triage the largest
   failure cluster first (`dEQP-VK.glsl.builtin.function` was the
   biggest in every partial sample so far). If the shell is gone,
   just relaunch it (see "next action" above) -- it no longer crashes
   partway (that was `L256`, already fixed), so a fresh full run is
   safe to kick off and forget for ~45-90 min.
2. **(unknown, new, ready to pick up)** `L260`: the residual
   `a2b10g10r10_snorm_pack32` alpha-channel bug split out this
   session. Start from its own `.qpa` diff-vs-threshold breakdown
   (alpha off by exactly 1.0, RGB fine) and look at
   `feme/lib/Graphics/ImageFixture.cpp`'s `packClearColor`/
   `unpackColor` 2-bit-alpha SNORM handling specifically -- check
   whether other 2-bit-SNORM-channel formats (if any exist) share the
   bug, to know if the fix should be narrow or general.
3. **(a few hours, still overdue, unchanged for several sessions)**
   `L228(e)`/`(f)`: `shader_render` (confirmed last session to not
   exist as a standalone group -- `dEQP-VK.glsl.*` is its modern
   replacement, which `L258` above covers) and most of `pipeline`'s
   other sub-suites remain unsampled at any real scale.
4. No git stashes left open this session (the A/B-test stash used
   during the false-alarm detour was popped immediately after use).
