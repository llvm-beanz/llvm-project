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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement roadmap milestone F8b and close out F8?

> **F8a's own remaining quarter: depth/stencil and multisample subpass-input
> local-read coverage.** `feme::vulkan::buildSubpassInputHeap`
> (`CommandBuffer.cpp`) already resolves
> `DepthInputAttachmentIndex`/`StencilInputAttachmentIndex` into heap slots, but
> (a) leaves a `SampleCount > 1` attachment's slot unpopulated rather than
> addressing its per-sample layout
> (`FemeImageDescriptor::SampleCount`/`MipLayouts::SampleStride` already model
> one, unused so far), and (b) has not been exercised against a real depth
> (`D16_UNORM`/`D32_FLOAT`) or stencil (`S8_UINT`) format at all -- a CTS-shaped
> test reading a depth/stencil input attachment back through
> `subpassLoad`/`OpTypeImage(Dim=SubpassData)`'s single-component form is needed
> to find whatever format-decode gap remains before either limit field can
> honestly flip to `VK_TRUE`
