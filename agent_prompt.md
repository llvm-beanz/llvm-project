---
model: claude-sonnet-5
resume: 1b814ab3-f423-4724-ae98-f676112844de
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

Please continue working on the FeMe Vulkan ICD. The previous session suggested
the next steps:

**Fastest next concrete action** (under two minutes): open
`feme/docs/Roadmap.md` at `ENV6(c)(2)(o)` and re-read this session's fresh
`FEME_DEBUG_ENV6C2O` trace findings (`.Flow227_crit_edge` has one real reachable
predecessor; `loop.exit.guard120` is also its own predecessor via a genuine
self-loop back edge) before designing a new matcher case -- do not reuse any
earlier session's draft point fix, it no longer applies.

1. **`ENV6(c)(2)(o)`** (needs its own full session): design a fresh fix for the
   now-current dominance-failure shape (two live, non-stale predecessors into
   `CheckExit->StayInLoop`, not a stale/unreachable one) -- a genuinely
   different case from both this session's and last session's drafts.
2. **`ENV6(c)(2)(l)`** (small audit, no known failing case): check whether the
   other 3 `closeLatch`/`freezeLoopCarriedValues` call sites have a similar
   latent `BodyMask`-vs-`NaturalContinue` gap, now that `L521`'s bug class is
   understood.
3. **`ENV6(c)(2)(c)`/`(d)`**: two isolated 1-case issues, not started.
4. **`ENV6(c)(3)`** (83 cases) / **`ENV6(d)`** (132 cases): own multi-session
   architecture efforts, not quick fixes.
5. **`ENV5(c)(7)(b)`** (hull lockstep barrier, 3 cases): lowest priority,
   unchanged for many sessions.
6. offload-test-suite upstreaming (3 patches): blocked on push access, unchanged
   for many sessions.
