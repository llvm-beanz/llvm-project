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

Can you work on H29d or other prerequisites blocking the H-series milestones?

> **Inline shader-module creation** (`VkPipelineShaderStageCreateInfo::module ==
> VK_NULL_HANDLE` with a chained `VkShaderModuleCreateInfo` in `pNext`,
> legalized for any pipeline stage once `VK_EXT_graphics_pipeline_library` is
> enabled): not implemented for either the compute (`compileComputePipeline`) or
> graphics (`compileGraphicsStage`) stage-compilation paths -- both currently
> reject a null `stage.module` cleanly rather than crashing (the compute-path
> guard landed as part of H29c's own crash-fix), but neither compiles the inline
> `VkShaderModuleCreateInfo::pCode` this now-real CTS surface exercises (e.g.
> `dEQP-VK.pipeline.pipeline_library.graphics_library.misc.non_graphics.shader_module_info_comp`).
> Needs a shared helper (usable from both the compute and graphics
> stage-compilation call sites) that, given a `VkPipelineShaderStageCreateInfo`
> whose `module` is null, walks `pNext` for `VkShaderModuleCreateInfo` and
> constructs an equivalent in-memory `vulkan::ShaderModule` without requiring a
> separate `vkCreateShaderModule` call
