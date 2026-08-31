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

Can you continue working on H19o or other prerequisites of the H-series
milestones from the roadmap?

> **The final `A2B10G10R10_{SNORM,SINT}_PACK32` gap in the mandatory
> `shaderStorageImageExtendedFormats` list**, split out of H19n once every other
> format in the real Vulkan spec's own mandatory-format table was closed. Needs
> 2 new `ResourceFormat` enum entries (appended at the tail, per this project's
> own append-only convention), `mapVkFormat`/`formatElementSize` wiring, and new
> `femeRTUnpackR10G10B10A2Snorm`/`femeRTPackR10G10B10A2Snorm` helpers in
> `FeMeRuntimeCPU.c` (the signed-normalized sibling of the existing
> `R10G10B10A2_UNORM` helpers H19n added, same MSB-down `A2B10G10R10` bit
> layout, clamped to `[-1.0, 1.0]`/scaled to a 9-bit signed range per component,
> mirroring `femeRTUnpackR8G8B8A8Snorm`'s own clamp-and-scale convention) --
> `_SINT` needs no new pack/unpack helper at all, only new dispatch-table
> wiring, since it is bit-for-bit identical to the already-implemented
> `R10G10B10A2_UINT` case (a signed/unsigned integer field's bit pattern is
> stored identically either way, matching every prior `_UINT`/`_SINT` pair in
> this project). Also needs the corresponding sampled-image feature-bit and
> `ImageFixture.cpp`/`feme-run.cpp` fixture-name cases, since neither format is
> mapped at all today. Only once this lands should
> `shaderStorageImageExtendedFormats` itself flip to `VK_TRUE`, and only then
> can H19i's own `without_format.*` work proceed.
