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

Can you work on H8o or other prerequisites blocking the H-series milestones?

> **Widen `ImageOps.cpp`'s blit-source support to BC4/BC5/BC6H, then flip
> `textureCompressionBC`.** H8n's own investigation found the mandatory format
> table backing `textureCompressionBC == VK_TRUE` requires `BLIT_SRC_BIT` on all
> 16 BC formats, but `runBlitImage`'s decode-then-resample pipeline is built on
> `feme::graphics::unpackColor`/`packClearColor`, which have no case for
> BC4/BC5/BC6H's own sampling-bridge targets
> (`R8_UNORM`/`R8G8_UNORM`/`R16G16B16A16_FLOAT`) -- `unpackColor`'s generic
> per-component path also requires its output array size to exactly match the
> format's own component count (so a 1-component `R8_UNORM`/2-component
> `R8G8_UNORM` cannot fill a 4-component RGBA blit buffer without a dedicated
> case, mirroring `A8_UNORM`'s own "missing channel reads as its identity value"
> precedent), and its generic float path additionally still truncate-copies a
> 2-byte half float into a 4-byte `float` variable rather than converting it --
> a separate, pre-existing bug this row's own investigation surfaced (affecting
> `R16G16B16A16_FLOAT`, and so BC6H once wired) that needs fixing before BC6H's
> own blit source can be trusted. Once all three gaps close, re-run the real
> `dEQP-VK.api.info.format_properties.compressed_formats`/`image_format_properties.*.bc*`
> groups to confirm the mandatory-format-table check actually passes before
> flipping `PhysicalDeviceInfo.cpp`'s `textureCompressionBC` to `VK_TRUE`
