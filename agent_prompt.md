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

Can you work on H95 or other blocking work to make progress on the H-series
milestones?

> **`properties.mesh_payload_size`/`task_payload_size`'s "Unexpected shared
> memory result: 0"** (now 6 cases: the original 2, plus all 4 of
> H94/H94a/H94b's own target cases --
> `mesh_payload_and_shared_memory_size`/`mesh_shared_memory_size`/`task_shared_memory_size`/`task_payload_and_shared_memory_size`
> -- newly exposed by H94b's own closing re-run once the `feme-cpu-wrap-entry`
> diagnostic that previously masked them was fixed, hitting the identical
> `vktMeshShaderPropertyTestsEXT.cpp:521` diagnostic): both compile, link, and
> run to completion (no crash, no pipeline-creation error) but fail a
> data-correctness check -- the CTS host-side verification reads back a
> shared-memory/payload value it expects to be nonzero (a marker the shader
> itself is supposed to have written) and finds 0 instead, suggesting either the
> write never happens, is masked away, or lands somewhere the read never sees.
> Not yet triaged -- needs its own IR/runtime reduction to determine whether
> this is a task/mesh payload or workgroup-shared-memory read/write plumbing
> gap, and whether it is the same root cause across all 6 cases or several
> coincidentally-identical symptoms (the original 2 cases use a payload-size
> property test with no verification loop at all, while the 4 newly-added cases
> use the shared-memory-size property test's own write-then-read-back loop that
> H94/H94a's own linearize work already touched once -- these are plausibly, not
> yet confirmed, two distinct bugs sharing one symptom)
