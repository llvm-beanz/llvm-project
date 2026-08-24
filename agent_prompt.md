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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

A bunch of the FeMeVulkanDesign has backed into odd corners as we've extended
the scope. Like the limiting of `subgroupSupportedStages` to compute-only. Since
FeMe should support Vulkan as a first-class runtime, we need to remove all the
assumptions in the FeMeVulkan design document around a compute-only device and
the assumptions in the FeMeCPUDesign as well.

Please do a pass over all the FeMe docs finding places where the scope was
limited to compute-only and adjusting the planned scope appropriately to support
a full graphics implementation.

Also please do a full Vulkan CTS run and update the CTS report with the current
state. Make sure that the current pass/fail/unsupported numbers are accurate on
the top of the CTS report file.
