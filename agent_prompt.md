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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H29c or other prerequisites blocking the H-series milestones?

> **Link-time merge and first real CTS re-run**: recognize
> `VkPipelineLibraryCreateInfoKHR::pLibraries` on a non-library
> `vkCreateGraphicsPipelines` call, gather every linked library's H29b-stored
> parts (plus any state provided directly by this call, for a
> partially-monolithic/partially-library mix), synthesize one complete
> `VkGraphicsPipelineCreateInfo`-equivalent state, and reuse the existing,
> unmodified `compileGraphicsPipeline` to produce a real executable pipeline --
> treating `PIPELINE_CONSTRUCTION_TYPE_LINK_TIME_OPTIMIZED_LIBRARY` and
> `FAST_LINKED_LIBRARY` identically internally, since a CPU-emulated ICD has no
> genuine fast-vs-optimized-link tradeoff to honor. Only once this is verified
> correct does `graphicsPipelineLibrary` flip to `VK_TRUE` and the extension get
> advertised, followed by a real `dEQP-VK.pipeline.pipeline_library.*` re-run
> (starting with a large reused-feature sub-group, e.g. `stencil`) to measure
> actual impact before declaring any part of H29 closed
