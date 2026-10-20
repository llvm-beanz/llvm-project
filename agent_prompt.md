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

1. **(~20-30 min)** Write the minimal `.ll` reproducer: a loop with a
   per-lane-divergent early-exit branch, where the *only* loop-carried value
   read after the loop is a boolean set to different literal constants on
   different paths (not a monotonic counter like `L464`'s case) -- feed it
   through `feme-opt --llvm -passes=feme-cpu-linearize,feme-cpu-simdize
   -feme-cpu-wave-size=4` and confirm the post-SIMD IR shows the same
   scalar-broadcast bug (a `<4 x i1>` that's just a `splat` of one scalar,
   not derived from the per-lane frozen value). Add as a
   `Transforms/CPU` lit test + `LinearizeTest.cpp`/`SIMDizeTest.cpp` unit
   test, confirmed to show the wrong IR shape on today's tree.
2. **(~30-60 min)** Fix in `Linearize.cpp`: give any loop-carried value set
   to different per-lane-divergent-path-dependent values inside the loop
   body its own frozen shadow phi (reuse/generalize `freezeLoopCarriedValues`'s
   existing mechanism), instead of letting it be represented by the
   structural exit-edge discriminator. **Watch for regressions** across the
   ~190 existing `Transforms/CPU` lit tests -- the discriminator-reuse
   behavior is *correct* for genuinely-uniform cases, so the fix needs to be
   scoped to only the divergent case, not a blanket change.
3. **(~15-20 min)** Re-validate: `check-feme`, the real
   `check-hlsl-clang-feme-vk` Mandelbrot test (confirm `PixelPercent` passes
   under 0.2%), and all 6 `assertion-crash-cases.txt` reproducers (no
   regression). Update `Roadmap.md` (strike `ENV3(c)` if closed) and
   `VulkanCTSReport.md`.
4. **(after ENV3(c) closes)** `ENV4`: full current CTS corpus collection
   (dedicated clean-host block, not an end-of-session afterthought --
   carried forward unchanged from every prior session).
5. **(after ENV4)** `ENV5`: partition the 103 SPIR-V instruction failures +
   4 graphicsfuzz failures into independently tested fixes.
6. **(low priority)** Upstream the 3 `offload-test-suite` patches if push
   access ever becomes available.
