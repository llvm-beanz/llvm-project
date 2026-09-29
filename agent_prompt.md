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

Can you continue working on the FeMe ICD implementation? The previous session's
suggested next steps are:

1. **(background, in progress, just needs monitoring)** The fresh
   full `dEQP-VK.glsl.*` sweep (28,420 cases) kicked off this session
   to reflect `L261`'s fix -- at last check (~mid-session) was at
   6,590/28,420 (~23%), still running at session end. Next session:
   check if it finished; if the shell's gone, just relaunch it (bash
   tool `mode="async"`, not shell backgrounding -- this convention is
   now well-established over several sessions, keep following it).
   Expected new tally roughly: ~17,867 Pass / ~1,590 Fail / ~8,963
   NotSupported (i.e., -598 Fail from the `L258` baseline, assuming
   no unexpected knock-on shifts elsewhere in the full sweep) -- worth
   confirming this matches once it completes.
2. **(a few hours, new, ready to pick up)** `L262`: the integer-
   sampler Bias/Grad/MinLodClamp gate in
   `SPIRVResourceLowering.cpp`'s `hasOnlySupportedImageUses`
   (`IsInteger` branch, ~line 1548-1673) still rejects `HasBias`/
   `HasGrad`/`HasMinLodClamp` for integer images -- this is the
   remaining ~128+ cases from the original int-sampler split in this
   session's own investigation (item 1 above). Would need: widening
   the gate, adding Bias/MinLodClamp operands to the 7
   `createSample*I32` builders (`ImageCalls.h`/`.cpp`), threading
   through `lowerImageAccesses`'s integer-shape dispatch, and updating
   `femeCpuImageSample2DV4I32`-family runtime functions in
   `FeMeRuntimeCPU.c` to use real (not stubbed) values.
3. **(unknown, still filed, not started)** `L263`: remaining
   untriaged smaller `L258` clusters (`builtin.function` biggest at
   403 in the partial sample, `atomic_operations` 96, `matrix` 92,
   etc.) -- re-triage against the fresh sweep's numbers once it's
   done, since `L261`'s fix may have shifted which cluster is
   actually biggest now.
4. **(carried over, unchanged)** `L260`: residual
   `a2b10g10r10_snorm_pack32` alpha-channel SNORM-packing bug --
   still not picked up, several sessions running now.
5. **(carried over, unchanged)** `L228(e)`/`(f)`: broader-than-
   tessellation/glsl CTS sampling (`pipeline`'s other sub-suites)
   still not done at real scale.
6. **(noted, not actioned)** `offload-test-suite`'s local `feme`
   checkout branch keeps drifting from `llvm-beanz/feme` between
   sessions (see item 9 above) -- re-verify/reset at the start of
   every session rather than trusting it's still intact from last
   time.
7. No git stashes left open this session (none were used -- this
   segment's work didn't need any A/B stash-based comparisons).
