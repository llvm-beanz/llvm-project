---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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

Can you work on L83 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`Basic/Matrix`'s own
> `matrix_groupthread_swizzle_{one,zero}_based`/`matrix_{m,one}-based_setter`
> cases fail a real numeric `BufferExact` mismatch check at execution time**,
> split out of L7a's own closing investigation this session: unlike the 4 cases
> L7a's own fix targeted, these 4 reach `vkCreateGraphicsPipelines`/pipeline
> execution successfully (no resource-normalization diagnostic, no
> `VkResult=-3`) but produce a wrong numeric result, strongly suggesting a
> matrix setter/groupshared-swizzle storage-order or indexing bug rather than a
> legalization/resource-lowering gap -- not yet reduced to a concrete IR-level
> cause this session; needs its own real IR reduction (following this project's
> own established reduce-first methodology, e.g. via `feme-translate
> --import-spirv`/`feme-opt --feme-convert-spirv-to-llvm` on one of these 4
> cases' own SPIR-V, then a runtime/CPU-value trace of the actual vs. expected
> buffer contents) to pin down the exact faulty component before scoping a fix
