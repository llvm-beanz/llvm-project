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

Can you work on H10i or other prerequisites blocking the H-series milestones?

> **`vkAcquireNextImage2KHR` is entirely unimplemented**, discovered by H10f's
> own real re-run once its matrix-arithmetic fix let
> `dEQP-VK.wsi.xcb.swapchain.render.basic2`/`2swapchains2` (and, by the same
> root cause, `10swapchains2`, already separately blocked at swapchain creation
> by H10g) clear pipeline creation and reach an acquire call using this entry
> point for the first time. Not a graceful "unsupported" rejection: `gdb`'s own
> backtrace shows a genuine `SIGSEGV` at address `0x0`, called directly from
> `vkt::wsi::multiSwapchainRenderTest<AcquireNextImage2Wrapper>`, meaning this
> ICD's own `vkGetDeviceProcAddr`/dispatch-table plumbing returns a null
> function pointer for this entry point rather than a real implementation, and
> CTS calls through it unconditionally once `VkAcquireNextImageInfoKHR`-based
> acquire is exercised (this ICD's advertised `apiVersion = VK_API_VERSION_1_4`
> obligates implementing this core-1.1 command regardless, per the same "every
> core command this ICD's own version claims must at least be present" precedent
> H10c's `vkEnumeratePhysicalDeviceGroups` family already established). Needs:
> (1) implementing `vkAcquireNextImage2KHR` itself (`Swapchain.cpp`, likely a
> thin wrapper around the existing
> `vkAcquireNextImageKHR`/`Swapchain::acquireNextImage` logic, since this ICD's
> single-physical-device-group scope (H10c) makes
> `VkAcquireNextImageInfoKHR::deviceMask` trivially satisfiable); (2)
> registering it in `EntryPoints.{h,cpp}`/`ImplementedEntrypoints.txt` so
> `vkGetDeviceProcAddr` actually resolves it instead of returning null; (3) a
> real re-run of `basic2`/`2swapchains2` (and `10swapchains2` once H10g's own
> swapchain-creation gap is separately fixed) to confirm a genuine, non-crashing
> result
