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

Can you continue working on H7s?

> **A `VkRenderPass` subpass's color attachment list with a
> `VK_ATTACHMENT_UNUSED` slot is rejected outright at pipeline-creation time.**
> Discovered via H7r's own re-run of `alpha_to_coverage_unused_attachment.*`
> (color output written to fragment location 1, location 0 explicitly unused):
> `GraphicsPipeline.cpp`'s `getRenderTargets` fails every case with `"an unused
> color attachment slot is not implemented"` once H7r's own format fix lets
> these cases clear the format gate for the first time -- a separate, larger,
> previously-untracked gap in subpass/render-target construction, unrelated to
> color-format support itself. Needs a real investigation into what
> `getRenderTargets`/the fragment-stage-output-to-attachment-slot mapping
> assumes about every slot being bound to a real attachment, and what has to
> change (likely treating an unused slot as "no attachment, but a real output
> location the shader may still write that is simply discarded") to support it
