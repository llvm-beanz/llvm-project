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

Can you work on H8i or other prerequisites blocking the H-series milestones?

> **BC1-7 compressed-format sampling** (deferred from H8c's own scoping pass).
> Needs its own `BCDecode.h`/`.cpp`, mirroring `ETC2Decode.h`'s own precedent (a
> standalone, directly-unit-tested, initially-unwired decoder) -- but BC1-5
> (comparable in complexity to ETC2's individual/differential modes) should
> likely be scoped and landed as their own slice before attempting BC6H (HDR,
> half-float endpoint interpolation) or BC7 (8 modes, variable partition counts,
> rotation, index selection), both dramatically more complex than anything
> ETC2/EAC or BC1-5 needed. `vktTextureCompressedFormatTests.cpp`'s own
> whole-family `textureCompressionBC` gate (confirmed by H8c's own
> investigation) means no CTS case in that specific group passes until all 16 BC
> formats this test group exercises are complete, so a BC1-5-only slice should
> expect the same "no CTS delta yet" story H8c's own ETC2/EAC slice had
