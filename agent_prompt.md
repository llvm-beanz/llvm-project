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
after each change and update the VulkanCTSReport.md.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement the C1 milestone for Vulkan conformance?

> **Mandatory formats.** Add `B8G8R8A8_UNORM` (and the rest of the Vulkan
> mandatory color-attachment/blend table) to `isSupportedColorAttachmentFormat`,
> and at least one combined depth+stencil format (`D24_UNORM_S8_UINT` or
> `D32_SFLOAT_S8_UINT`) to
> `isSupportedDepthAttachmentFormat`/`isSupportedStencilAttachmentFormat`,
> backing each with a real pack/unpack path in `feme::graphics`. This is the
> cheapest step by far and unblocks every Amber-based CTS test, which is most of
> the CTS's own end-to-end coverage
