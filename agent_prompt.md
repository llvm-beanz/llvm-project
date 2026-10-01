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

1. **(half a day to a day, best next pick, new)** `L319`: find and
   close the gap between `feme-run`'s own JIT-path resource
   normalization and `feme::vulkan`'s real `vkCreateComputePipelines`
   path for a register-bound sampled-image handle. Start by comparing
   `feme::cpu::runPipeline`'s own pass list against whatever
   `feme::vulkan` calls for compute-pipeline creation (`GraphicsPipeline.cpp`'s
   compute counterpart, if one exists separately) -- `H8`'s own root
   cause (a missing `CanonicalizeStagePass` call) is the first thing to
   check for by analogy. Once closed, also fix the SIMD-path
   silent-zero gap found alongside it (small, same area).
2. **(a few hours, once `L319` is closed)** Resume `L315`/`L318`'s
   actual full-pipeline repro: the real `SampleCmpGrad`/`Texture1D`/
   `SamplerComparisonState` shader (already written this session,
   not committed since it currently fails for the wrong reason) should
   finally be runnable once `L319` lands; use it to capture the real
   bug's shape end-to-end for the first time this investigation.
3. **(a few hours, carried over, unchanged)** `L317`: `texel_buffer`'s
   10 remaining functional failures.
4. **(a few hours, carried over, unchanged)** `L316`:
   `texture.explicit_lod.2d.sizes.*` (16 cases) and
   `texture.multisample` (5 cases).
5. **(a few hours, lower priority, many sessions carried over,
   unchanged)** `L265`'s ASTC alpha-decode tie-break (4 block sizes).
6. **(~21 cases, a few hours, carried over, unchanged)**
   `rasterization`'s remaining 82 stipple/adjacency Bresenham failures.
7. **(low priority, pre-existing, unrelated to FeMe, unchanged)**
   `offload-test-suite`'s `spec_const_32_bits.test`/
   `WaveActiveMax.test` lit-annotation issues, and
   `array_of_matrices.test`'s unexpected-pass.
8. **(housekeeping, overdue, last done 5 sessions ago at `d0974dd`)**
   `check-hlsl-feme-vk`/`offload-test-suite` `feme`-branch-drift check
   -- not checked again this session.
