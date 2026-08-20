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

Can you implement milestone E2 in the roadmap document?

> **Wire the aggregate `VkPhysicalDeviceVulkan13Properties`/`Vulkan14Properties`
> `vkGetPhysicalDeviceProperties2` cases**, enumerating all 70 mandatory limit
> fields from `Vulkan14FeatureInventory.md`'s table. Most are either a real
> minimum this ICD can already compute (e.g. `maxBufferSize`,
> `storageTexelBufferOffsetAlignmentBytes`) or a truthful `VK_FALSE`/`0` for a
> capability not yet implemented (every `integerDotProduct*Accelerated` bit
> until E8 lands, `maxPushDescriptors` until F12 lands). Land the struct case
> with every field set to a conservative, honest value first, then let each
> later row (E8, F5, F11, F12, ...) raise its own subset once the feature behind
> it is real, instead of blocking this row on every other one
