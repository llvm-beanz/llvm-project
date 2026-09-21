---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

**Always**: Add thoughts and next steps to the end of the agent_thoughts.md
file.

# Request

Can you continue the work on feme? The last agent's suggested next steps are:

1. **Check `/tmp/ctsrun/l128/blend_full.qpa`** (PID 40501 if still
   alive) for the `pipeline.monolithic.blend.*` sweep's final tally
   before doing anything else CTS-related -- it was still running (0
   Fail through 32,000+ cases) when this session ended. If clean,
   update `VulkanCTSReport.md`'s note on this and consider the
   long-standing "is `blend.*` actually clean" question finally
   closed for good.
2. **`L128(a)` (the nondeterministic JIT crash) needs real
   memory-instrumentation tooling** before anyone re-attempts the
   loop-unrolling half of `L128` -- `apt install valgrind` (not present
   this session) or an ASan-instrumented build of the CPU JIT path is
   the natural next step. Do not re-attempt the unroll pass by pure
   inspection again; that approach is exhausted for this bug.
3. Once `L128(a)` is understood, `L128` itself just needs the
   (now-reverted) unroll pass re-derived on top of a fix for whatever
   `L128(a)` turns out to be, plus a final CTS check of the 3 target
   `query_max_attributes.*` cases.
4. `L125(m)`/`L125(n)` (upstream MLIR+LLVM `ConstOffsets` plumbing)
   remains the largest not-yet-started cross-repo item -- needs its own
   dedicated session, not a quick pick.
5. `L115(b)` (pull-model interpolation) remains flagged from several
   sessions ago as needing a new runtime-callback ABI surface -- also
   not a quick pick.
6. `ninja check-feme` and both CTS build directories (`VK-GL-CTS`,
   `llvm-project`) are incremental from here -- no reconfigure needed.
7. This session's scratch CTS logs at `/tmp/ctsrun/l128/*` (including
   the still-running blend sweep's log) should be cleaned up by
   whichever future session confirms the blend sweep's final result
   and no longer needs the raw log.
