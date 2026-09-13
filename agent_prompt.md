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

Can you work on H101q or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.{nested_structs_instance_arrays.{2,15,31},basic_instance_arrays.32}`'s
> 8-case unresolved stage-IO global-variable reference** (newly characterized
> during H101m's own closing re-triage): each of these 4 named cases fails
> identically in both its `random_geometry` and `random_vertex` variant, but
> with a *different* diagnostic depending on which stage hits it first -- the
> `random_geometry` variant reaches JIT link time and fails with `JIT session
> error: Symbols not found: [ spirv_var_N ]` (an unresolved global reference
> baked all the way through to the final compiled module), while the
> `random_vertex` variant is instead caught earlier by
> `feme-graphics-validate-stage`'s own explicit diagnostic (`function 'main' has
> an unresolved stage-IO global-variable access to 'spirv_var_N', a shape
> CanonicalizeStagePass does not yet canonicalize into a 'feme.stage.*' call`)
> -- almost certainly the same underlying "some shape `CanonicalizeStage.cpp`'s
> `addElements`/`TakeBlockPath` doesn't yet recognize is left un-rewritten" root
> cause as every previous JIT-symbols-not-found bucket this milestone has hit
> (H101k's own filing, H101m's own closing note), just not yet re-triaged
> against the current binary to confirm which specific shape trips it this time.
> Not yet triaged -- needs each of the 4 named cases' own decompiled SPIR-V
> pulled and diffed against every shape `TakeBlockPath`/the plain (non-block)
> path already handles, to identify the one attribute (nesting depth,
> array-of-struct-of-struct, or similar) still missing
