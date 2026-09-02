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

Can you work on H8j or other prerequisites blocking the H-series milestones?

> **Wire the now-complete `ETC2Decode.h` decoder into a real consumer and flip
> `textureCompressionETC2`.** H8c landed a complete, directly-unit-tested
> ETC2/EAC decoder, but nothing calls it yet -- `Format.cpp` has no
> `ResourceFormat` enumerators for any of the 10
> `VK_FORMAT_ETC2_*`/`VK_FORMAT_EAC_*` formats, `mapVkFormat` has no cases for
> them, and `vkCreateImage` still rejects every one outright. Needs the same
> wiring shape roadmap E22 gave `ASTCDecode.h`: `Format.{h,cpp}` block-aware
> layout entries, `CommandBuffer.cpp`/`ImageOps.cpp` decode-on-sample-or-copy
> plumbing, and `PhysicalDeviceInfo.cpp`'s `textureCompressionETC2` bit, only
> flipped once a real `dEQP-VK.texture.compressed_format.*` ETC2/EAC case is
> confirmed passing end to end

