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

1. **H121** (~30-60 min, not yet triaged): `per_patch_block`'s own 9 cases
   now run to completion but fail at `vk.queueSubmit(...):
   VK_ERROR_INITIALIZATION_FAILED at vkCmdUtil.cpp:338`. Start with a
   validation-layer message or `gdb` backtrace through `feme-vulkan`'s own
   `vkQueueSubmit` entry point.
2. **H117/H118** (~1-2 hours, real IR-reduction needed): re-run the same
   `feme-translate --import-spirv`/`feme-opt -passes=feme-graphics-
   canonicalize-stage` technique H115 used, but on `per_patch_block_array`/
   `per_vertex_block`'s own real SPIR-V, since H115's fix does not cover
   whatever their own `spirv_var_43`/`spirv_var_31` shape actually is.
3. **H116** (~45-60 min): `per_patch_array.*`, "Invalid input value" --
   different error class, still not triaged at all.
4. **H119** (~45-60 min): isolines-only image comparison failures (6
   cases) -- use H88's own channel-level pixel-reduction technique.
5. **`offload-test-suite`'s `check-hlsl-feme-vk` target**: still never
   built/run in any session on record (well over a dozen sessions
   deferring it now) -- worth a dedicated session of its own.
6. Clean up `/tmp/h120_*` scratch files (low priority, not part of the
   repo) -- `/tmp/h120_preopt/pre_opt_2_1_main.ll` specifically is worth
   keeping a copy of if further Linearize/SIMDize work is anticipated,
   since it's a proven, real, minimal (1-violation) reproducer.

