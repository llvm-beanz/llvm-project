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

1. **Reduce and root-cause L111** (`unusual_multisample_state`) --
   ~1-2 hours. Confirmed unrelated to `gl_ViewIndex`/multiview; not yet
   reduced or triaged. This is the last failure in
   `pipeline_library.graphics_library.*`; closing it means that whole
   836-case group goes fully clean (Pass/NotSupported only, plus the
   pre-existing benign timing warning).
2. **Then broaden the sweep again (roadmap L106)** -- same untriaged
   candidates noted for several sessions running: a fresh `pipeline.*`
   subgroup (`pipeline.monolithic.*`, `pipeline.multisample.*`) or a
   top-level group outside `pipeline.*` (`subgroups.*`, `compute.*`,
   `graphicsfuzz.*`). ~30-60 minutes to pick the cheapest-looking one.
3. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
4. **Technique confirmed again this session**: reusing an existing,
   near-identical multiview `DrawTest.cpp` test as the starting point for
   a new flag-gated variant (copy the whole test body, add one line
   setting `PipeInfo.flags`, change the expected per-layer color) is much
   faster than building integration test scaffolding from scratch --
   worth doing again whenever a fix only changes one flag/bit's effect on
   an already-tested code path.

