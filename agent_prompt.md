---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Can you continue working on H7r?

> **`VK_FORMAT_R5G6B5_UNORM_PACK16` (and likely other packed 16-bit formats) has
> no support anywhere in the image/format layer**, discovered via H7n's own real
> CTS re-run of
> `dEQP-VK.pipeline.monolithic.multisample.alpha_to_coverage_unused_attachment.*`
> (`AlphaToCoverageColorUnusedAttachmentInstance`'s own hard-coded color
> format), which fails every feme-supported-sample-count case at `vkCreateImage`
> time with `VK_ERROR_FORMAT_NOT_SUPPORTED`. Unrelated to alpha-to-coverage's
> own coverage-mask logic (confirmed: the identical mask computation already
> passes 12/12 real cases against ordinary formats in H7n's own main group),
> purely a pre-existing, generic packed-format gap in the format-mapping/image
> layer. Needs a real survey of which packed 16-bit formats (`R5G6B5`,
> `R5G5B5A1`, `B5G6R5`, etc.) are worth adding, then real support through the
> format-mapping, image storage, and sampling/blending paths
