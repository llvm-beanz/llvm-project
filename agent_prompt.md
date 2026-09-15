---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
---
# Initial Guidelines

Please make sure that your changes are appropriately tested with unit tests
covering each phase of translation in the compiler, and that your changes
conform to the [LLVM Coding Standards](llvm/docs/CodingStandards.rst).

Also please review the feme/.instructions.md file.

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

# Request

Can you work the H-series milestones?

The last session suggested the next steps:

2. **H124j** (~1-2 hours): missing GLSL.std.450 legalization for
   `Cross`/`Reflect`/`Distance`/`FindUMsb`/`FindILsb` (5 cases). Same
   shape as H124f's already-fixed `Normalize`/`Length`/`IsNan`/`IsInf`
   — look at that fix first, likely directly extensible.
3. **H124l** (~1-2 hours): `GroupNonUniformQuadSwap` has no
   legalization pattern (6 cases, all `WaveOps/QuadReadAcross*`).
4. **H124o** (~1-2 hours): push-constant struct GEP out-of-bounds (2
   cases) — check whether it's the same declared-vs-physical
   member-index remap H128/H129/H131/H133 already fixed for UBO/SSBO.
5. **H124g's untriaged ~18 cases**: needs `FEME_VULKAN_LOG_CREATION_ERRORS=1`-
   style fresh triage, especially the 7 `Textures/*` pipeline-creation
   failures (no specific opcode/error captured yet) and the two
   `VK_ERROR_VALIDATION_FAILED_EXT` device-creation failures (need
   `-validation-layer`'s actual message, not just the VkResult code).
6. **H124d/H124e** (large, unchanged for many sessions, confirmed still
   real): upstream MLIR SPIR-V derivative ops and CPU
   divergence-handling gaps, respectively. Still deprioritized/multi-
   session efforts.
7. Lower priority, deferred 15+ sessions: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s heap corruption.
