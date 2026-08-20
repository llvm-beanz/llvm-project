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

Can you complete milestone D1 on the roadmap?

> **An accurate 1.3/1.4 mandatory-feature/limit/extension inventory.**
> `vk_gen_entrypoints.py`'s `CORE_FEATURES` now resolves through
> `VK_VERSION_1_3` (D0), but `VK_VERSION_1_4` is not yet included, and no
> promoted-1.3/1.4 feature struct
> (`VkPhysicalDeviceVulkan13Features`/`Vulkan14Features` and their per-extension
> originals -- `dynamicRendering` is already advertised via its pre-promotion
> `VK_KHR_dynamic_rendering` path, but `synchronization2`,
> `maintenance4`/`5`/`6`, `subgroupSizeControl`, `shaderIntegerDotProduct`,
> `pipelineCreationCacheControl`, `pushDescriptor`, and the rest are not) has
> been audited against what claiming 1.4 actually requires. This is D0's own
> "measure honestly" step turned into a checklist: enumerate the full set from
> `vk.xml` itself (the same way `vk_gen_entrypoints.py` already resolves
> `CORE_FEATURES` transitively), rather than re-deriving it by hand the way C6
> did for 1.2's much shorter list
