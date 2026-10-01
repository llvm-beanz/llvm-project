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

1. **(a few hours, lower priority, carried over many sessions)** `L265`'s ASTC
   alpha-decode tie-break -- confirmed last session to affect 4 block sizes
   (`astc_5x5`/`astc_8x8`/`astc_10x5`/`astc_12x12`), not 2. Still no structural
   difference found between passing/failing cases; a reference-decoder
   comparison (Mesa/lavapipe) is still the most promising untried angle. No new
   ideas this session (did not revisit).
2. **(carried over many sessions)** `fragdepth`/general multisample
   image-creation gap (`L280`/`L281`, the "group (a)" half of `L307`'s own
   original 222-case finding, 36 cases, still open) and the
   combined-depth-stencil-format `readDepth`/`writeDepth` bug (`Executor.cpp`
   ~line 999, 6 cases). The `x8_d24_unorm_pack32` `NotSupported` cases seen in
   this session's own re-run are this exact gap.
3. **(overdue many sessions)** `copy_and_blit.core.image_to_image.*` (52,692
   cases) and the remaining `copy_and_blit` top-level groups (`copy_commands2`,
   `dedicated_allocation`, `multiplanar_xfer`, `copy_memory_indirect`,
   `device_address`, `sparse`, `dynamic_state`, `reinterpret`) are still
   unsampled, as is `image_clearing` (45,636 cases) entirely. Not attempted this
   session -- stayed focused on `L307` per the request.
4. **(low priority, pre-existing, unrelated to FeMe)** `offload-test-suite`'s
   own `spec_const_32_bits.test`/`WaveActiveMax.test` lit-annotation issues, and
   `array_of_matrices.test`'s unexpected-pass. Reconfirmed present, unchanged,
   still needs upstream fixes outside this project's scope.
5. **(housekeeping, due again in ~5 sessions)**
   `check-hlsl-feme-vk`/`offload-test-suite` `feme`-branch-drift check -- not
   checked this session (branch untouched), file away for a routine check soon.
