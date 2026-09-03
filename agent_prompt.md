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

Can you work on H10b or other prerequisites blocking the H-series milestones?

> **A real `deqp-vk` build and a real `dEQP-VK.wsi` group re-run** against
> H10a's new `VK_KHR_xcb_surface` backend, split off from H10a once its own
> platform-surface scope closed. No prebuilt `deqp-vk` binary exists under
> `/home/dev/dev/VK-GL-CTS/` in this environment, and building the full CTS
> framework from source (a large, multi-dependency C++ project with its own
> build system) was judged out of scope for a single session -- needs its own
> feasibility check (build-time budget, missing system dependencies) before a
> real pass-rate measurement of `dEQP-VK.wsi` against a genuine platform surface
> can happen for the first time

If there are missing system dependencies to build and run the CTS, can you
please add a section to the Vulkan CTS report document detailing the missing
dependencies and install them. The documentation will allow me to update the
configuration scripts for the environment in the future.
