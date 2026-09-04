---
model: claude-sonnet-5
resume: ec2f5570-263a-4b95-917f-6c2230e594cf
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

Can you work on H10g or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.wsi.xcb.swapchain.render.10swapchains`/`10swapchains2` fail
> `vkCreateSwapchainKHR` with `VK_ERROR_INITIALIZATION_FAILED`**, discovered by
> H10d's own real re-run once its `CompositeConstruct` fix let these two cases
> clear pipeline creation and reach swapchain creation itself for the first time
> (previously masked by the shader-legalization failure the two share with
> H10f's own four cases). No further diagnostic is logged
> (`FEME_VULKAN_LOG_CREATION_ERRORS`-visible or not) -- `vkCreateSwapchainKHR`'s
> own explicit rejection paths (`Swapchain.cpp`, array-layer count, image
> extent, format) all look satisfied by this test's own request, and CTS's own
> `multiSwapchainRenderTest<...>(..., 10u)` creates 10 real swapchains (one per
> real xcb window) where `2swapchains`/`2swapchains2` (already known-blocked by
> H10f, not this row) create only 2 -- suggesting a real, count-dependent
> resource limit somewhere in this ICD's own xcb window/surface/swapchain
> creation path that only 10 concurrent instances trip. Needs a real
> investigation (most likely inside `XcbSurface.cpp`/`Surface.cpp`'s own
> window-creation code, or a fixed-size table somewhere in `Swapchain.cpp`) into
> what specifically fails once a 10th concurrent swapchain/window is requested
