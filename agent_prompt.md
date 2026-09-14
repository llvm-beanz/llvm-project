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

# Request

Can you work the the H-series milestones?

The last session suggested the next steps:

1. **H7w** (`_dynamic_index`, 32/128 passing) and **H7x**
   (`_fragmentshader_read`, 50/64 passing) are the only remaining
   blockers on `shaderClipDistance`/`shaderCullDistance`. Both already
   have their own roadmap rows with partial-fix history -- worth a fresh
   session picking up exactly where their own rows leave off, ~1-2 hours
   each for a real diagnostic given the rows are already partially
   fixed.
2. **`_dynamic_index` combined with `vert_geom`/`vert_tess_geom`** still
   crashes at pipeline creation with `"JIT session error: Symbols not
   found: [ spirv_var_N ]"` -- a distinct, unrelated, not-yet-filed bug,
   flagged again this session (third session in a row to notice it and
   defer it). Worth its own ~15-30 min first diagnostic, and probably its
   own new milestone row once triaged.
3. **`offload-test-suite`'s `check-hlsl-feme-vk` target** is still never
   built/run in any session -- flagged again as a standing gap (fourth
   session in a row). Worth a session of its own to wire it up.
