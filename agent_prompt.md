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

1. **Reduce and root-cause L114** (~1-2 hours). Get a
   `--deqp-log-shader-sources=enable` dump or SPIR-V disassembly of one
   reduced `sample_position.correctness.128_128_1.samples_2` case to see
   exactly which fragment input triggers `"element 1 has no location"`
   -- almost certainly a `gl_SamplePosition`-adjacent builtin the SPIR-V
   importer or `StageLink.cpp`'s interface-matching validation doesn't
   recognize as system-value-linked, wrongly treating it as an ordinary
   `Location`-based varying with no matching vertex-stage output. This
   is core Vulkan 1.0 (not extension-gated), so the fix must actually
   make it work, not just report `NotSupported`.
2. **Re-sweep `multisample_shader_builtin.*` after L114 lands** --
   expect all 95 cases to resolve to 55 Pass / 0 Fail / 40 NotSupported
   (the 40 NotSupported are a separate, not-yet-confirmed-benign bucket
   -- worth a quick spot-check of a couple to confirm they're a real
   capability gap, not another undiscovered bug, before assuming so).
3. **Continue the L106 sweep** after L114 closes: same untriaged
   candidates noted for several sessions running --
   `multisample-interpolation.txt` (247 cases, small, also topically
   related) is the cheapest next pick; `pipeline.monolithic.*`/
   `subgroups.*`/`compute.*`/`graphicsfuzz.*` are much larger and still
   untriaged.
4. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
5. **Technique confirmed again this session**: when a CTS pipeline-
   creation `Fail` gives only a generic `VK_ERROR_INITIALIZATION_FAILED`
   in the batched sweep log, `FEME_VULKAN_LOG_CREATION_ERRORS=1` plus a
   single-case rerun reliably surfaces the real underlying `error:`
   string -- used three times this session (L112's `unhandled
   Decoration`, L113's `partial VkSampleMask`, L114's `no location to
   link`), and each time the string alone was enough to identify a
   completely different root cause and fix location, without needing
   any temporary code instrumentation.
6. **Technique confirmed**: before assuming a CTS gap needs a *feme*
   code change, `grep` the relevant MLIR upstream code path first
   (`mlir/lib/Target/SPIRV/...`) -- L112 turned out to already be fully
   handled on feme's own side (`SPIRVToLLVMPatterns.cpp`,
   `CanonicalizeStage.cpp`), with the actual gap one layer further
   upstream than feme's own codebase. Saved significant time versus
   assuming the bug was in feme's own importer.
