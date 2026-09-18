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

1. **Implement L110** (~half a day, well-scoped already in Roadmap.md). Start
   at `GraphicsPipelineState` in `GraphicsPipeline.cpp`: add
   `PreRasterViewIndexIsDeviceIndex`/`FragmentViewIndexIsDeviceIndex` bools,
   set from `CreateInfo.flags` (non-linked path) and from each linked
   library's own `Pipeline::createFlags()` (linked path). Then thread a
   per-stage-group override into wherever `ViewIndex` is currently written
   into the ABI invocation records (`CommandBuffer.cpp`'s per-view loop,
   `Executor.cpp`).
2. **Re-sweep `pipeline_library.graphics_library.*` after L110 lands** --
   expect the 6 `view_index_from_device_index_in_*` cases (12 counting
   `_link_time_opt` siblings) to flip from Fail to Pass, landing at
   548/1/287/1 (only `unusual_multisample_state`, L111, still failing).
3. **Reduce and root-cause L111** (`unusual_multisample_state`) -- confirmed
   unrelated to gl_ViewIndex/multiview, not yet touched.
4. **Standing gotcha, still true**: export
   `VK_ICD_FILENAMES=/home/dev/dev/llvm-project/build2/tools/feme/tools/feme-vulkan/feme_icd.json`
   before any `vulkaninfo`/`deqp-vk` in a fresh shell -- not persisted.
5. **Technique confirmed this session**: when a CTS `SelfValidate` test gives
   you `Fail` with no diagnostic message, decode the QPA's embedded base64
   PNGs directly (Python + Pillow) -- but check for a `Description` field
   describing a `p'=p*scale+offset` normalization first, and reverse it,
   before concluding anything about the decoded colors.
