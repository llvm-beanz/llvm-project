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

Can you work on H91 or other blocking work to make progress on the H-series
milestones?

>  **`task_shared_memory_size`/`synchronization.other.barrier_across_secondary`'s
>  "mesh output wrapper requires attached feme.signature metadata"** (now 5
>  cases: the original 2 -- one newly exposed by H74's specialization-constant
>  fix, the other by its rasterizer-discard fix -- plus 3 more
>  (`properties.mesh_payload_size`/`task_payload_size`/`task_payload_and_shared_memory_size`)
>  newly exposed by H89a/H89b's own closing re-run, hitting the identical
>  diagnostic): `MeshOutputWrapper.cpp` expects `feme.signature` metadata to
>  already be attached to the mesh entry point by the time it runs, which is
>  missing for all 5. Not yet triaged -- needs its own reduction to find why
>  signature-metadata attachment is skipped/delayed for these specific shapes
