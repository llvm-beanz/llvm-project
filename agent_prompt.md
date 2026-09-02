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

Can you work on H6r or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.mesh_shader.ext.api.draw.*`'s own
> `no_task_shader`/`no_task_shader_secondary_cmd` direct-draw variants with
> `draw_count > 0` (44 of the 540-case `dEQP-VK.mesh_shader.ext.api.*` group,
> found re-running H6p's own fix) now reach rendering (no crash, no
> pipeline-creation failure) but fail a pixel comparison** -- `draw_count_0`
> (nothing drawn) already `Pass`es, and the *task*-stage-driven equivalents
> already fail earlier at H6q's own `vkCreateGraphicsPipelines` gap rather than
> reaching rendering at all, so this is specifically the "one or more direct
> `vkCmdDrawMeshTasksEXT` calls, no task shader" shape's own
> rendering-correctness gap -- not yet triaged for root cause (could be
> `gl_DrawID`-adjacent, since these are exactly the cases this milestone's row's
> own fix newly unblocked, or could be a preexisting, unrelated "multiple direct
> mesh draws in one render pass" gap this group is simply the first to exercise;
> needs its own reduction to tell which). Squarely inside this milestone's own
> existing "bounded payload/output limits reported truthfully"
> rendering-correctness scope, mirroring H6m/H6n/H6o/H6p's own precedent of
> tracking an in-scope rendering `Fail` as its own row rather than silently
> absorbing it into a CPU-lowering fix's own row
