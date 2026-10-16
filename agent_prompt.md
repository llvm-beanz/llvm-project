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

# Request

The last session ended without committing results. Any intermediate results are
stored in a git stash.

Can you please continue the work on the Vulkan FeMe ICD? A previous session
suggested the next steps:

1. **(~2-4 hours)** Mandelbrot flake -- still not picked up, several
   sessions running now. Low priority, change of pace.
2. **(standing, unresolved many sessions)**
   `backup-before-rebase-20261014` tag decision,
   `CMAKE_BUILD_TYPE=RelWithDebInfo` vs `Release` decision -- neither
   revisited this session either.
3. **(new this session)** `offload-test-suite` checkout: local `feme`
   branch is 7 commits ahead *and* 1 commit behind
   `llvm-beanz/feme` (diverged, not just stale) -- the single
   remote-only commit (`0cc94c2`, "Add FeMe test targets") only touches
   `CMakeLists.txt`/`test/CMakeLists.txt`/`test/lit.cfg.py`, and the 3
   previously-found drift fixes (`VkBool32`, `WaveActiveMax` XFAIL,
   `array_of_matrices` XFAIL) are all still present locally, so this is
   not the standing drift pattern recurring -- but a future session
   should decide whether to rebase/merge that one remote commit in, or
   confirm it's already superseded locally in a different form, before
   trusting the local checkout's CMake wiring indefinitely.
4. No remaining roadmap item currently known to block anything in the
   `variablePointers`/milestone-9 area -- `K6a`'s full tree (`K6n`
   through `K6z`, `L430`) is now closed end to end. Next open item with
   its own next-step text is whichever `L428`-family or standing item
   the next session's own prior-session summary names.
