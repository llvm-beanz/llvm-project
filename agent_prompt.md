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

Can you continue working on H7j or other prerequisites of the H-series
milestones from the roadmap?

> **`shaderStorageImageExtendedFormats`/`shaderStorageImageMultisample`/`shaderStorageImageReadWithoutFormat`/`shaderStorageImageWriteWithoutFormat`**:
> no `OpImageRead`/`OpImageWrite` lowering was found anywhere in the transform
> path for a storage image at all, so these four format/configuration bits have
> no shader-side storage-image read/write implementation to be honest about yet
> -- a larger prerequisite than a narrow format restriction to lift. Needs
> storage-image read/write lowering built first (likely its own, larger
> milestone), with these four bits as a follow-on once it exists (split out as
> its own top-level milestone, H19, rather than nested further under H7 -- H19a
> now closes the base Plain2D/mandatory-format-floor read/write case; H19d
> specifically tracks this row's own remaining four feature bits, which need
> format/configuration breadth beyond what H19a itself claims)
