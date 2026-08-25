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
after each change and update the VulkanCTSReport.md. Please keep the
Vulkan14FeatureInventory and VulkanExtensionInventory up to date with each
change as well.

If the request is to complete a roadmap stage, if you complete it please strike
it through on the roadmap document, if you do not, please add entries to the
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement roadmap milestone F11? Tne last agent prompt left the work
incomplete, its results are in the git stash at index 0.

> **`VK_EXT_host_image_copy`/`hostImageCopy` + its 6 limit/list fields.** The
> largest single new mechanism in this section:
> `vkCopyMemoryToImage`/`vkCopyImageToMemory`/`vkCopyImageToImage`/`vkTransitionImageLayout`
> copy/transition without a command buffer at all, needing a host-side (not
> executor-queued) path into `Image.cpp`'s existing layout/format machinery;
> `pCopySrcLayouts`/`pCopyDstLayouts` (per D1's own finding, an enumerated list,
> not a scalar limit) is the supported-layout list this new path accepts
