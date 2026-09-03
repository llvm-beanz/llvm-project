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

Can you work on H10a or other prerequisites blocking the H-series milestones?

> **A real CI-exercisable platform surface** (H10's own headless surface is not
> one -- it never actually presents anywhere a CI run could observe) and a real
> `deqp-vk` re-run of the whole `dEQP-VK.wsi` group, split off from H10 once its
> own headless/swapchain scope closed. Needs, at minimum: (1) picking exactly
> one platform backend genuinely exercisable in this project's own CI, per
> FeMeVulkanDesign.md's own "chosen by CI, not by preference" decision
> (`VK_KHR_xcb_surface` or `VK_KHR_wayland_surface` are the two candidates that
> decision already names; `VK_KHR_display` is explicitly out of scope, "Initial
> Non-Goals" already excluding the external-memory/modifier negotiation a
> direct-mode/cross-driver-sharing backend would need) -- needs its own
> feasibility check against whatever this environment's CI actually runs on; (2)
> building that backend's own `Surface` variant reusing H10's existing
> `Swapchain` state machine (which does not assume headless specifically) and
> its "presenting a host-memory image is a blit reusing `vkCmdCopyImage`'s own
> copy path" design; (3) a real `deqp-vk` build (none exists in this environment
> yet, per every prior CTS-run note in this file) to actually measure
> `dEQP-VK.wsi`'s pass rate against a real surface for the first time
