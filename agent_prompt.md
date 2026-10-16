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

1. **(~15 min)** Drop the one-line "the prior session's work was lost"
   friction going forward: consider ending every session with an explicit
   `git commit` of `agent_thoughts.md`/doc changes as the very last action,
   even if mid-investigation, so a hard stop never loses doc work again
   (code-level WIP can stay stashed, but doc bookkeeping is cheap to commit
   early and often).
2. **(~2-4 hours)** Still-open from prior sessions: nothing new is blocking.
   The `L428`-family and `K6n`-proper items referenced in earlier session
   next-steps were already closed out per the `L430`/`L428(a)` entries --
   re-check `Roadmap.md` for any remaining non-struck-through row before
   assuming there's a specific next target; if none, the project may be at
   a natural checkpoint for a fresh triage pass (e.g. a new raw CTS sweep
   compared properly against Pass-count baselines this time) rather than
   chasing a named item.
3. **(standing)** Keep re-checking `offload-test-suite`'s `feme` branch for
   the safety-net-timeout fix every session -- it lives outside this repo's
   history and the every-session reset (`agent-setup.sh`) only restores it
   via the cherry-pick + the 4 known drift-fix commits, which were all still
   present and did not need redoing this session.
