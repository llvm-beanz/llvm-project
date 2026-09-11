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
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H69 or other blocking work to make progress on the H-series
milestones?

> **`dEQP-VK.mesh_shader.ext.in_out.32_bits_only.permutation_*.{mesh_only,task_mesh}`
> (80 cases, found by H31's own closing full-group re-run)**: every
> I/O-permutation case in this bucket fails a pixel comparison
> (`vktMeshShaderInOutTestsEXT.cpp:1590`), across both direct (`mesh_only`) and
> task-driven (`task_mesh`) dispatch, i.e. this is not the same "direct
> multi-draw" shape H31 covered -- it exercises a different mesh/fragment
> varying-linking combination (many permutations of scalar/vector I/O types and
> counts) that still mismatches even with H31's two fixes in place. Confirmed
> pre-existing (reproduces identically against the pre-H31-fix build, not a
> regression from H31's own changes). Not yet triaged for root cause -- needs
> its own IR/case reduction, following the same technique H6/H21/H30's own
> chains have used throughout, to isolate which specific I/O shape(s) within the
> permutation space actually break
