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

Can you work on H10e or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.wsi.xcb.swapchain.acquire.too_many` fails**: `"Implementation
> failed to respond well acquiring too many images with 0 timeout"`, discovered
> by H10b's own real, now-non-crashing `dEQP-VK.wsi.xcb.*` re-run (the 8th and
> last of its 8 real failures, and the only one not sharing H10d's own
> `render.*`-shaped root cause). Needs a real investigation into
> `vkAcquireNextImageKHR`'s (`Swapchain.cpp`) own handling of a zero-timeout
> acquire once every swapchain image is already acquired/unavailable -- CTS
> expects a specific, spec-defined error response (most likely `VK_NOT_READY`,
> the documented zero-timeout-and-nothing-available result) that this ICD's
> current logic does not produce
