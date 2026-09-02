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

Can you work on H6u or other prerequisites blocking the H-series milestones?

> **With H6s and H6t both closed, a real `deqp-vk` re-run of
> `dEQP-VK.mesh_shader.ext.api.draw.draw_count_0.no_indirect_args.no_count_limit.no_count_offset.with_task_shader`
> still fails `vkCreateGraphicsPipelines`, now with a new, distinct, and
> unrelated error**: `"a stage's root-constant span is not fully covered by a
> VkPushConstantRange visible to it in its VkPipelineLayout"` -- confirmed (via
> `grep`) to originate in `feme/lib/Vulkan/GraphicsPipeline.cpp`'s own
> push-constant-range pipeline-layout validation
> (`validateStageInterfaces`-adjacent code, with a second occurrence in
> `feme/lib/Vulkan/Pipeline.cpp`), a completely different subsystem than any of
> H6s/H6t's own compiler-pass changes -- a
> Vulkan-API/pipeline-layout-validation-level gap, not a compiler bug. Not yet
> triaged at all: needs to confirm whether the CTS-supplied `VkPipelineLayout`'s
> own `pPushConstantRanges` genuinely does not cover the task stage's own
> root-constant span (a real CTS-side or spec-reading gap on this project's own
> part), or whether `GraphicsPipeline.cpp`'s own coverage check is itself too
> strict (e.g. failing to account for a task stage's own
> `VkShaderStageFlagBits::VK_SHADER_STAGE_TASK_BIT_EXT` specifically, or
> mis-computing the task stage's own root-constant span now that H6t's `DrawID`
> field occupies what was previously reserved/unused padding). Confirmed present
> across all 58 `with_task_shader`/`with_task_shader_secondary_cmd` cases in the
> 540-case `dEQP-VK.mesh_shader.ext.api.*` group (the same bucket H6q/H6s/H6t
> have each progressively unblocked one layer deeper)
