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

Can you work on H8n or other prerequisites blocking the H-series milestones?

> **Wire the now-complete BC1-7 decoders (`BCDecode.h`, `BC7Decode.h`,
> `BC6HDecode.h`) into a real consumer and flip `textureCompressionBC`.**
> H8i/H8l/H8m together landed complete, directly-unit-tested decoders for all 16
> `VK_FORMAT_BC*` formats, but nothing calls any of them yet -- `Format.cpp` has
> no `ResourceFormat` enumerators for any BC format, `mapVkFormat` has no cases
> for them, and `vkCreateImage` still rejects every one outright. Needs the same
> wiring shape roadmap E22 gave `ASTCDecode.h` and H8j is expected to give
> `ETC2Decode.h`: `Format.{h,cpp}` block-aware layout entries,
> `CommandBuffer.cpp`/`ImageOps.cpp` decode-on-sample-or-copy plumbing, and
> `PhysicalDeviceInfo.cpp`'s `textureCompressionBC` bit, only flipped once a
> real `dEQP-VK.texture.compressed_format.*` BC1/BC7/BC6H case is confirmed
> passing end to end (`vktTextureCompressedFormatTests.cpp`'s own whole-family
> gate means partial wiring of just one BC sub-family will not move any CTS case
> by itself -- all three decoders' own wiring needs to land together, or at
> least BC1-5's own wiring alone needs to be enough to flip the bit before
> BC7/BC6H's own wiring follows)
