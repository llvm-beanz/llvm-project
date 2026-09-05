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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L27 or other prerequisites blocking the L-series milestones?

> **2 of L22's own 13 cases
> (`Feature/Semantics/{DomainSystemValues,HullSystemValues}.test`) now clear the
> array-of-struct `CompositeConstruct` legalization but still fail
> `vkCreateGraphicsPipelines`, `VkResult = -3`, on a distinct, later gap**:
> `"feme-cpu-simdize: function 'main' has a divergent vector value '' used
> outside a supported
> insertelement-chain/resource-store/extractelement/select/shufflevector/phi/elementwise/comparison/reduce/vectorizable-intrinsic
> pattern; component decomposition is not yet supported for this use (roadmap
> milestone 7 deviation)"` -- a real, pre-existing `SIMDize.cpp` limitation,
> explicitly flagged in its own diagnostic as a milestone-7 deviation, now
> reached for the first time by these two cases once the array-of-struct
> construct they use stopped failing earlier in the pipeline. Needs its own real
> IR reduction (most likely `HullSystemValues.test`, the smaller of the two) to
> identify the exact divergent-vector-producing pattern `SIMDize.cpp`'s
> component-decomposition path does not yet cover, before designing a fix
