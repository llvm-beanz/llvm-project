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

1. **(~1 day, start fresh)** `synchronization.op.*` is now 0 Fail. Pick a
   *different* CTS test family for the next raw sweep-and-triage pass -- e.g.
   the broader `api.*`/`copy_and_blit`/`image_clearing` families `L302`
   originally sampled from, or a fresh full mustpass run -- rather than
   continuing to mine this now-closed family.
2. **(standing, many sessions now)** `Basic/Mandelbrot.test`'s flake (in
   `offload-test-suite`, exercised via `check-hlsl-feme-vk`) -- still not picked
   up as its own item this session either. Low priority, change-of-pace
   candidate.
3. **(standing)** `offload-test-suite` `feme`-branch-vs-`main` drift --
   re-checked this session (cheap `git fetch`/`git log` only, no rebuild): still
   7 ahead / 1 behind, same single remote-only commit (`0cc94c2`, touches only
   `CMakeLists.txt`/`test/CMakeLists.txt`/`test/lit.cfg.py`), unchanged from the
   prior session's finding. Still needs a decision (rebase it in vs. confirm
   superseded) from a session that's actually touching `offload-test-suite`.
4. **(risk worth a stress test, not urgent)** The `canReachBlock` confined-BFS
   fallback was validated against this one repro shape plus the
   now-fully-passing `synchronization.op` sweep. A future session touching
   `Linearize.cpp` again should keep an eye out for any case with multiple
   nested *sibling* child cycles interacting with this same staleness mechanism
   differently -- not hit in this session's testing, but not exhaustively
   stress-tested either.
