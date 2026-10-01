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

**Always**: Make sure you are not using precompiled headers in your build.

# Request

Please continue working on the FeMe Vulkan ICD. The previous session's suggested
next steps are:

1. **(a few hours, lower priority, carried over many sessions, unchanged)**
   `L265`'s ASTC alpha-decode tie-break (4 block sizes affected:
   `astc_5x5`/`astc_8x8`/`astc_10x5`/`astc_12x12`). Still needs a Mesa/lavapipe
   reference-decoder comparison -- no new ideas this session (not revisited).
2. **(overdue many sessions, next best pick for broader sampling)**
   `copy_and_blit`'s remaining top-level groups are still entirely unsampled:
   `copy_commands2`, `dedicated_allocation`, `multiplanar_xfer`,
   `copy_memory_indirect`, `device_address`, `sparse`, `dynamic_state`,
   `reinterpret`. `image_clearing` is now done (this session, clean).
   `copy_and_blit.core.image_to_image.*` (52,692 cases) specifically is still
   the single largest unsampled chunk.
3. **(a few hours)** `synchronization.op` (20,131 cases) is still unsampled --
   needs sub-batching by its own sub-prefixes (too large for one run, same
   reasoning as `image_clearing` before this session showed it's actually fine
   to just run in one shot if given enough wall-clock time -- ~5 minutes for 45k
   cases here, so `synchronization.op` at 20k should be similarly tractable in
   one run now; worth just trying it directly next time rather than
   pre-emptively sub-batching).
4. **(low priority, pre-existing, unrelated to FeMe, unchanged)**
   `offload-test-suite`'s own `spec_const_32_bits.test`/`WaveActiveMax.test`
   lit-annotation issues, and `array_of_matrices.test`'s unexpected-pass.
   Reconfirmed present this session (now against the bumped `d0974dd` tip too),
   still needs upstream fixes, outside this project's scope.
5. **(housekeeping, just done this session, due again in ~5 sessions)**
   `check-hlsl-feme-vk`/`offload-test-suite` `feme`-branch-drift check -- bumped
   to `d0974dd` this session (25 commits of upstream drift, mostly new test
   coverage: MSAA, viewport arrays, HLSL intrinsic tests). Baseline Pass/Fail
   counts updated (727 total now, up from 697) but the known-issue set is
   unchanged.
