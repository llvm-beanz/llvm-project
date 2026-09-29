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

1. **(a few hours, largest untriaged chunk)** `L263`: re-triage the
   `L258`/`L261`/`L262`/`L264`-era residual `dEQP-VK.glsl.*` fail clusters
   against a fresh full sweep (last real full-sweep tally was `L262`'s
   17,867/1,590/8,963 -- this session's fix should shave off another ~24, worth
   confirming with a fresh run rather than assuming). `builtin.function` (403 in
   the last partial sample) and `atomic_operations` (96) are the two biggest
   untriaged buckets.
2. **(unknown, several sessions carried over)** `L260`:
   `a2b10g10r10_snorm_pack32` alpha-channel SNORM-packing bug in
   `ImageFixture.cpp`'s `packClearColor`/`unpackColor`. Still not started.
3. **(a few hours, overdue for many sessions)** `L228(e)`/`(f)`:
   broader-than-glsl/tessellation CTS sampling (`pipeline`'s other sub-suites,
   `api`, `synchronization`) at real scale -- still not done.
4. **(low priority, not a regression)** `offload-test-suite`'s own
   `Feature/SpecializationConstant/spec_const_32_bits.test`/`WaveOps/WaveActiveMax.test`
   (failing) and `Feature/PushConstant/array_of_matrices.test` (stale `XFAIL:`)
   still need upstream lit-annotation fixes -- unrelated to any FeMe/LLVM
   change, flagged repeatedly across sessions so it's not mistaken for new
   breakage.
5. No git stashes left open this session (none were used -- the
   `offload-test-suite` branch happened to already be in sync, so no merge/stash
   cycle was needed this time either).
