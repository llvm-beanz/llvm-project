---
model: claude-sonnet-5
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

Can you work on H93 or other blocking work to make progress on the H-series
milestones?

> **`properties.max_mesh_output_primitives_256`'s pixel-comparison mismatch** (1
> case, newly exposed by H89a/H89b's own closing re-run): compiles and runs to
> completion (no crash, no pipeline-creation error, confirmed not a hang) but
> fails its own image comparison (`Check log for details at
> vktMeshShaderPropertyTestsEXT.cpp:1287`) -- the sibling case
> `max_mesh_output_vertices_256` (same H89a/H89b masked-loop fix, analogous
> large-output-array shape) now passes outright, so this is not simply "the same
> loop bug again"; likely a distinct, narrower issue specific to the primitive-
> rather than vertex-output path. Not yet triaged -- needs a channel-level pixel
> reduction, mirroring the technique H88's own closing session used for the
> analogous `local_size_id_mesh`/`local_size_id_task` rows, to determine the
> real vs. expected framebuffer content and narrow down which stage of the
> primitive-output path disagrees
