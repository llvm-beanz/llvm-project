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

Can you implement milestone E21 in the roadmap document?

> **`VK_EXT_texture_compression_astc_hdr`/`textureCompressionASTC_HDR`** (E15's
> original scope, unchanged): the 14 additional HDR block formats
> (`VK_FORMAT_ASTC_{4x4,...,12x12}_SFLOAT_BLOCK_EXT`), reusing E20's shared
> bit-unpacking/weight-grid decode and adding the HDR-only float color-endpoint
> decode path
