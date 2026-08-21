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

Can you implement milestone E24 in the roadmap document?

> **`vkGetPhysicalDeviceImageFormatProperties`/`vkGetPhysicalDeviceFormatProperties`
> are still pre-V5 stubs**, a genuine, pre-existing gap roadmap E22's own CTS
> run found rather than introduced (`git blame` confirms both predate this row's
> own commits by 10+): `vkGetPhysicalDeviceFormatProperties` unconditionally
> reports an all-zero `VkFormatProperties` and
> `vkGetPhysicalDeviceImageFormatProperties` unconditionally returns
> `VK_ERROR_FORMAT_NOT_SUPPORTED`, for *every* `VkFormat`, not only a
> block-compressed one -- their own comments still say "no image is supported
> yet (images are out of scope before V5)", stale since V5 landed real image
> support. This is why E22's own CTS run measured zero headline movement despite
> `textureCompressionASTC_LDR` now reading `VK_TRUE`: `dEQP-VK.texture.*`'s own
> capability probe (`vktTextureTestUtil.cpp`) calls
> `vkGetPhysicalDeviceImageFormatProperties` before creating any image, of any
> format, and this stub fails it unconditionally -- so no
> texture-creation-shaped CTS case can pass regardless of which formats/features
> this ICD actually implements. A real implementation needs to report accurate
> `VkFormatFeatureFlags`/`VkImageFormatProperties` (max extent, mip levels,
> array layers, sample counts, `formatElementSize`/`bytesPerBlock`-derived
> resource size) for every format `mapVkFormat` recognizes, gated on
> `usage`/`tiling`/`flags` the same way `isValidImageShape` (Image.cpp) already
> gates `vkCreateImage` -- a substantial, separate subsystem in its own right,
> well beyond this row's own ASTC-specific scope
