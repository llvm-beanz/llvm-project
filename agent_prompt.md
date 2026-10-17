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

1. **(~1-2 days, dedicated session, needed before any further L419(z)/(aa)/(ab)
   attempt)** Design the per-lane loop-carried-value freeze mechanism first, in
   isolation: when `HeaderDivergent` is set but Header's own divergent exit
   funnels back into the loop body rather than leaving the cycle directly, every
   loop-carried phi value needs a `select`/freeze gated on
   `active.header.check.live`, analogous to `DiamondFlattener`'s existing
   in-body diamond merges. Prove this with a standalone unit test (assert the
   expected `select` appears) before touching `recoverRelayExitCheck` again.
   Then re-apply the `IsTrivialExitStub`/`RelayBlock` fixes from this session
   (they're correct and still needed, just not sufficient alone) and re-validate
   the full 552-case sweep plus the 4+8 target cases plus all previously-fixed
   `L419` cases.
2. **(standing)** `mixed_break_continue` (4 cases) — separate `DiamondFlattener`
   body-shape gap, untouched, repro at `/tmp/mbc_frag_mod2.ll`.
3. **(standing)** `L419(y)`'s masking-sequencing gap (`conditional_break`, 6
   cases) — untouched.
4. **(standing)** `L419(g)` — 30 tessellation residuals, untouched.
5. **(half day)** `check-hlsl-clang-feme-vk`'s 92-failure baseline re-triage,
   still deferred every session.
6. **(check-in, cheap, every session)** `/tmp/l437_full_run3/` still running and
   growing (375MB) — just check progress, don't restart.
7. **(when the sweep finally completes)** Replace `VulkanCTSReport.md`'s whole
   master per-group table in one pass using the completed results.
