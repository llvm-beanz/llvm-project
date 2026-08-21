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

Can you implement milestone E20 in the roadmap document?

> **Block-compressed image groundwork + ASTC LDR decode**, the prerequisite
> E15's own investigation found missing: (1) extend
> `feme::cpu::ResourceFormat`/`Image.{h,cpp}`'s subresource layout math from its
> current per-texel stride to a block-based one (block-dimension-aligned extent,
> bytes-per-block instead of `formatElementSize`, still computed once at
> `vkCreateImage` time per V5's existing "packed table" design); (2) a real ASTC
> bitstream decoder (integer sequence/trit-quint decoding, all weight grid and
> color-endpoint-mode combinations, 1-or-2 partitions, dual-plane, void-extent
> blocks) for the 14 LDR-only block footprints
> (`VK_FORMAT_ASTC_{4x4,5x4,5x5,6x5,6x6,8x5,8x6,8x8,10x5,10x6,10x8,10x10,12x10,12x12}_UNORM/SRGB_BLOCK`),
> gating the previously-untracked
> `VkPhysicalDeviceFeatures::textureCompressionASTC_LDR` bit (a Vulkan 1.0 core,
> not 1.3/1.4-promoted, feature — add it to `Vulkan14FeatureInventory.md`'s
> tracked set since this investigation is what surfaced that it had no row at
> all). This is the largest single new subsystem in this whole roadmap
> (comparable in scope to a from-spec software codec) and should be scheduled as
> such rather than folded into a "small extension" lane
