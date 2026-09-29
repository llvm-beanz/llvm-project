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

1. **(a few hours, new, ready to pick up)** `L264`: the same
   Bias/Grad/MinLodClamp widening for `Cube`/`CubeArray` int-sampler
   shapes (~24 cases, ~3% of the original gap's population). Reuse
   `femeRTComputeCubeUVDerivatives` for the derivative math. Remember
   the `matchImageCall` lesson from this session and last: when
   widening a builder, its independent matcher switch in the same
   file needs the same operand-count/index update, and it fails
   *silently* (returns `std::nullopt`), not loudly, if missed.
2. **(unknown, still filed, not started)** `L263`: remaining
   untriaged `L258`/`L261`-era clusters (`builtin.function`,
   `atomic_operations`, `matrix`, etc.) -- re-triage against this
   session's fresh 1,590-fail tally, since `L262`'s fix may have
   shifted which cluster is now biggest.
3. **(carried over, unchanged, several sessions running)** `L260`:
   residual `a2b10g10r10_snorm_pack32` alpha-channel SNORM-packing
   bug in `ImageFixture.cpp`'s `packClearColor`/`unpackColor`.
4. **(carried over, unchanged)** `L228(e)`/`(f)`: broader-than-
   glsl/tessellation CTS sampling (`pipeline`'s other sub-suites)
   still not done at real scale.
5. **(new, low priority, out of scope for a FeMe-focused session)**
   `offload-test-suite`'s own `Feature/SpecializationConstant/
   spec_const_32_bits.test`, `WaveOps/WaveActiveMax.test` (both
   actually failing) and `Feature/PushConstant/array_of_matrices.test`
   (unexpectedly passing, i.e. a stale `XFAIL:`) need their upstream
   lit annotations refreshed -- this is `offload-test-suite`
   test-infra bookkeeping unrelated to any FeMe or LLVM code change
   made in this or recent sessions, flagging only so it isn't
   mistaken for a new regression next time `check-hlsl-feme-vk` is
   run.
6. **(re-confirmed, standing risk)** `offload-test-suite`'s local
   `feme` checkout branch has now drifted from `llvm-beanz/feme`
   in *two different ways* across recent sessions (a hard external
   reset to `origin/main`, and this session's silent fall-behind on
   a critical single commit) -- the standing instruction to
   re-verify/reset at the start of every session remains essential;
   consider making this an explicit numbered pre-flight step (like
   the `vulkaninfo` check) rather than something that only gets
   caught incidentally when a next step happens to touch it.
7. No git stashes left open this session (none were used).
