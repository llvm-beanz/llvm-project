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

1. **(~2-3 hours, the real next step, hard blocker for L419(w)/L419(x))**
   Fix `flattenTaintedBranchesInRegion`'s `PreRegion`/`PostRegion`
   candidate walk to skip a block whose `CI.getCycle(BB)` differs from
   the cycle currently being linearized. Rebuild, re-run the targeted
   10 cases (`conditional_break_*`, `dowhile_trap_*`,
   `select_iteration_count_*`), then the full 552-case sweep -- target
   the 10 regressed cases back to Pass with zero new regressions to the
   already-fixed 6.
2. **(~1 day, after #1 if `L419(x)` closes cleanly)** Take a second look
   at `conditional_break_{fragment,vertex}`'s own still-open diagnosed
   failure -- its widened `CheckBlock` path is supposed to mask it
   correctly per the `L419(t)`/`L419(w)` widening comment, so the
   cycle-containment check's rejection is itself likely masking a
   separate, more specific shape gap in `flattenKnownDivergentBranch`
   that's worth understanding rather than permanently routing around.
3. **(~half day)** Set up `offload-test-suite`'s cmake build (branch
   `feme` already checked out) and get `check-hlsl-feme-vk` actually
   running at least once -- every session so far has deferred this.
4. **(standing)** `L419(g)` (tessellation residuals, 30 cases) --
   untouched, same IR-dump-and-reduce methodology candidate.
5. **(standing)** `L419(h)`'s 12 remaining special-loop residuals
   (`elseblock`/`ifblock`/`mixed_break_continue`) -- distinct shapes,
   need their own reduction.
6. **(check-in, cheap, every session)** `/tmp/l437_full_run2/` is
   stalled -- either restart it fresh or abandon it and start a new
   sweep; don't just re-check the same dead directory again.
