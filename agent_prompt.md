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

Can you continue working on H19g or any prerequisite work required to complete
the H-series milestones?

> **`shaderStorageImageMultisample`**, split out of H19d's own original bundled
> scope. `dEQP-VK.image.load_store_multisample.*` (252 cases) is still all
> honestly `NotSupported` on this bit today (confirmed via a probe of
> `load_store_multisample.2d.r32_uint.samples_2`, unaffected by H19d's own
> cube/cube-array closure). Needs `classifyStorageImage2DHandle` to accept `MS
> == 1`, a per-sample coordinate component the runtime's fetch/store helpers do
> not take today (no `feme.cpu.image.*` entry point accepts a sample index), and
> `PhysicalDeviceInfo.cpp` to flip the feature bit plus raise whatever
> `VkPhysicalDeviceLimits` sample-count field gates it once real
