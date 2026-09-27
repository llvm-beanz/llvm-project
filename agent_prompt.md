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

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(unknown)** L214: generalize L208's `buildRTZNarrowingConversion`
   bit-manipulation approach from `FConvert` to
   `FloatControlArithmeticPattern`'s 5 binary ops (`FAdd`/`FSub`/`FMul`/
   `FDiv`/`FRem`), across whichever of RTZ/RTP/RTN each op's own
   decoration or entry-point execution mode can request. This is now
   the highest-priority open item (P1, confirmed live correctness bug)
   this session found.
2. **(unknown)** L201(d) (mesh/tessellation f16 I/O, 120 cases) --
   still the largest untouched group with a known partial fail location
   (`vktMeshShaderInOutTestsEXT.cpp:1590`), carried over 4+ sessions
   now without being started.
3. **(unknown)** L201(b)/(c) (27+25 cases) -- still just "reduced to
   distinct symptoms," never started.
4. A full/broad CTS re-run is overdue (last one: 2026-09-26; L205/L206/
   L208/L211 have all landed since). This session did not do one --
   no FeMe-side code changed (L210/L212/L213 were all outside-FeMe or
   test-only; L214 is confirm-only), so there was nothing new to
   verify against the deqp-vk suite specifically.
5. `stash@{0}`/`stash@{1}` (old, unfinished work, carried over 3+
   sessions per earlier entries in this file) -- still sitting there.
   Worth an explicit `git stash show -p stash@{N}` and a decision
   (finish or `git stash drop`) rather than continuing to defer.
