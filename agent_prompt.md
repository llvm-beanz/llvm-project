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

Can you work on H8r or other prerequisites blocking the H-series milestones?

> **`b8g8r8a8_srgb` (`VK_FORMAT_B8G8R8A8_SRGB`) is an entirely unmapped format,
> split off from H8g.** A real `deqp-vk` run (against feme's own ICD, see H8g's
> closure note) found this format missing every mandated bit -- `mapVkFormat`
> has no case for it, so `formatFeatureFlags` never even runs for it (the
> `Format ? formatFeatureFlags(*Format) : VkFormatFeatureFlags(0)` fallback in
> `EntryPoints.cpp` returns zero features for any unrecognized `VkFormat`). A
> real fix needs a new `ResourceFormat::B8G8R8A8_UNORM_SRGB` enumerator
> (appended at `RuntimeABI.h`'s own tail) mirroring `B8G8R8A8_UNORM`'s existing
> byte-swap-of-`R8G8B8A8_UNORM_SRGB` relationship, then wiring it through every
> file that already special-cases `R8G8B8A8_UNORM_SRGB`/`B8G8R8A8_UNORM`
> (`ImageFixture.cpp`, `Executor.cpp`, `RenderPass.cpp`, `CommandBuffer.cpp`,
> `BCSamplingBridge.cpp`/`ETC2SamplingBridge.cpp` if their own sRGB-decode paths
> apply, `Format.cpp`) before `formatFeatureFlags` can honestly grant it
> `BLIT_SRC/DST_BIT`/`SAMPLED_IMAGE_BIT`/`SIFL`/`COLOR_ATTACHMENT_BIT`
