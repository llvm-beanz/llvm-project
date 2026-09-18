---
model: claude-sonnet-5
resume: 52e661a0-b284-45ee-892f-3073721ca338
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

1. **Root-cause L115** (~1-2 hours to scope, unknown to fix -- a new
   SPIR-V extended-instruction import, likely a nontrivial chunk of
   work once scoped). Per the L112 precedent, check upstream MLIR's
   own GLSL.std.450 import path first (`mlir/lib/Target/SPIRV/...`,
   look for how `InterpolateAtCentroid`/`interpolateAtSample`/
   `interpolateAtOffset` extended instructions are (or aren't) handled)
   before assuming the gap is in feme's own `SPIRVToLLVMPatterns.cpp`.
   `FEME_VULKAN_LOG_CREATION_ERRORS=1` plus a single reduced case rerun
   (the L112/L113/L114 technique, confirmed useful again three
   sessions running) should surface exactly which of the 3 opcodes is
   hit first and where.
2. **Re-sweep `multisample_interpolation.*` after L115 lands** --
   expect most of the 115 failures to flip to Pass; worth also
   re-checking the 12 that already passed and the 120 NotSupported to
   make sure L115's fix doesn't touch their classification.
3. **Continue the L106 sweep after L115 closes**: `pipeline.monolithic.*`/
   `subgroups.*`/`compute.*`/`graphicsfuzz.*` remain the large,
   untriaged candidates noted for several sessions running -- still no
   session has picked one of these up yet, worth prioritizing one of
   them next specifically to break the multi-session `pipeline.*`-only
   pattern.
4. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
5. **Technique confirmed again this session**: `deqp-vk`'s
   `--deqp-caselistfile` flag does not exist (despite looking like the
   obvious name) -- use `-n "case1,case2,..."` (comma-joined, supports
   wildcards) instead; saved a round-trip of guessing flag names.
