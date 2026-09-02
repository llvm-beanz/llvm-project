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

Can you work on H8b or other prerequisites blocking the H-series milestones?

> **Remaining mandatory `VERTEX_BUFFER_BIT` format families.** H8a scoped
> `isVertexBufferFormatSupported` to exactly the 17 formats `Executor.cpp`'s
> `decodeAttribute` already implements -- the Vulkan spec's full 45-format
> mandatory vertex-buffer list also requires the 8-bit `R8_*`/`R8G8_*` family (8
> formats), the 16-bit `R16_*`/`R16G16_*`/`R16G16B16A16_*` family (15 formats),
> and `A2B10G10R10_UNORM_PACK32` (1 format) -- all confirmed still genuinely
> failing `dEQP-VK.api.info.format_properties.*` after H8a (6 of the 16-bit
> cases seen directly: `r16_{unorm,snorm}`, `r16g16_{unorm,snorm}`,
> `r16g16b16a16_{unorm,snorm}`). Needs
> `decodeAttribute`/`attributeComponentByteSize` (Executor.cpp) expansion for
> each new scalar type, mirroring the existing `R8G8B8A8_*` cases mechanically
