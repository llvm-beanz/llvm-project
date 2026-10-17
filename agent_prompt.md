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

1. **(~1 day)** The actual capability fix for `L419(z)`'s shape: make
   `collectUniformPassThroughRegion` (or `matchExitCheck`'s relay-recovery)
   tolerate a successor chain that passes through another block with its own
   `CondBr` before reaching `ExitBlock`, by recursively masking that
   intermediate block's divergent split too. This is materially riskier than
   today's fix — this exact code area (`L419(v)`/`L419(w)`) has regressed twice
   before when generalized carelessly. Validate against the 4 loop-in-switch
   cases plus the full 552-case `loops.special` sweep and the already-fixed
   `L419(w)`/`L419(x)` cases (16 total) before considering it safe.
2. **(standing)** `L419(g)` — 30 tessellation residuals, untouched.
3. **(standing)** `L419(h)`'s other remaining residuals
   (`elseblock`/`ifblock`/`mixed_break_continue`, 12 cases) — distinct shapes,
   need their own reduction.
4. **(standing)** `L419(y)` (`conditional_break_uniform`, 6 cases) — untouched.
5. **(half day)** `check-hlsl-clang-feme-vk`'s 92-failure baseline re-triage,
   still deferred every session.
6. **(check-in, cheap, every session)** `/tmp/l437_full_run3/` is still running
   and growing (294MB as of this session's end, up from 253MB) — just check
   progress, don't restart.
7. **(when the sweep finally completes)** Replace `VulkanCTSReport.md`'s whole
   master per-group table in one pass using the completed results.
