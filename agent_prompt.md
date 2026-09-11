---
model: claude-sonnet-5
resume: 0a6535de-11d2-4cb7-8770-7e69bf31da83
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
agent_thoughts.md file.

# Request

Can you work on H92 or other blocking work to make progress on the H-series
milestones?

> **`properties.max_mesh_output_size_with_payload_per_{primitive,vertex}_no_view_index`/`max_mesh_output_size_without_payload_per_{primitive,vertex}_no_view_index`'s
> `feme-graphics-validate-stage: ... unresolved stage-IO global-variable access
> ...`** (4 cases, newly exposed by H89a/H89b's own closing re-run): a mesh
> entry has a stage-IO global-variable access `CanonicalizeStagePass` does not
> yet canonicalize into a `feme.stage.*` call, so `ValidateStagePass` correctly
> (per its own H6g-b-c precedent) rejects it at compile time instead of letting
> it reach the JIT as an undefined symbol -- the same diagnostic class as H76's
> own `smoke.fast_lib.*` row, but not yet confirmed to be the identical
> root-cause shape (these are `properties.*` mesh-output-payload-size cases, not
> `smoke.fast_lib`'s fragment-library-sharing shape). Not yet triaged -- needs
> its own IR reduction of one `_no_view_index` case to identify the specific
> global-variable access shape `CanonicalizeStagePass` misses, and to
> confirm/refute overlap with H76 before any new fix work
