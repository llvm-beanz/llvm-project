---
model: claude-sonnet-5
resume: 6e011932-a46a-43ae-97b3-283c96c999ff
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

Can you please work on the FeMe ICD implementation? The previous session gave
the next steps:

1. **(highest value, ~1-2 hrs)** Root-cause `multiple_descriptor_sets`:
   start by reading `feme/lib/Vulkan/Descriptor*.cpp`'s
   `vkUpdateDescriptorSets`/`image_heap`-population path, comparing a
   single-descriptor-set case against a two-descriptor-set case to find
   where the second set's binding gets lost before it ever reaches the
   shader. The shader IR is confirmed correct (see above), so this is a
   Vulkan-layer bug, not an LLVM-pass bug -- don't waste time back in
   `SPIRVResourceLoweringPass`/`Linearize.cpp` for this one.
2. **(~1-2 hrs, once above is fixed or as a standalone task)** `L176`:
   generalize `SPIRVUnmergeResourceLoadsPass` to correctly re-thread the
   merge through a sunk load instead of just declining to rewrite it.
   This would restore `vertex_fragment.*` support (121 `storage_image`
   cases alone, likely several hundred once other binding types are
   counted). More invasive than this session's fix -- needs a real
   design for how to reconstruct the merge at the phi's own block and
   propagate it through whatever extra blocks the load was sunk across.
3. **`binding_model_shader_access`/`descriptorset_random`
   (198 fails)/`inline_uniform_blocks` (9 fails)** -- still not triaged
   at all, mentioned by prior sessions, still waiting.
4. **`L125(m)`/`L125(n)`** (upstream MLIR+LLVM `ConstOffsets` plumbing)
   -- still the largest not-yet-started cross-repo item, good for a
   change-of-pace session.
5. **(~5 min)** Clean up `/tmp/l175_*` scratch (PNG dumps, `.ll` dumps,
   `.log` files) -- everything worth keeping is already quoted in
   `VulkanCTSReport.md`/this file. Also delete
   `/tmp/Pipeline.cpp.presession_bak` (no longer needed, both reverts
   already `diff`-verified clean).

