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

Can you work on H6p or other prerequisites blocking the H-series milestones?

> **With H6o closed, the full `dEQP-VK.mesh_shader.*` group now runs to
> completion for the first time (no crash), surfacing its own new dominant
> `Fail` bucket: all 216 `dEQP-VK.mesh_shader.ext.api.draw.*` cases fail
> `vkCreateGraphicsPipelines` with `VK_ERROR_INITIALIZATION_FAILED`**, root
> cause confirmed directly via `FEME_VULKAN_LOG_CREATION_ERRORS=1 deqp-vk`:
> `feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh output
> wrapper` (`MeshOutputWrapper.cpp`'s `lowerMeshStageOps`, the same catch-all
> H6g-b-d already narrowed to only genuinely-unlowered `feme.stage.*` calls) --
> confirmed on
> `dEQP-VK.mesh_shader.ext.api.draw.draw_count_0.no_indirect_args.no_count_limit.no_count_offset.no_task_shader`
> (a mesh-only entry, no task shader), meaning this is a *real*, still-unlowered
> `feme.stage.*` op reaching this pass, not a recurrence of H6g-b-d's own
> already-fixed over-broad rejection. Not yet triaged for which specific op
> (needs its own IR reduction, mirroring the H6g-b/H6j/H6k/H6l/H6n/H6o
> technique, to isolate exactly which `feme.stage.*` call the
> `dEQP-VK.mesh_shader.ext.api.draw` group's own shaders emit that neither
> `OutputStore` nor `SetMeshOutputs` covers -- `EmitMeshTasksEXT`'s own
> still-uncanonicalized form, per `MeshOutputWrapper.cpp`'s file comment, is a
> candidate but this specific case has no task shader at all, so it is likely a
> different, not-yet-identified op). Two smaller, separate, and likely unrelated
> buckets the same re-run also surfaced (left untriaged and unfiled pending H6p,
> to avoid over-fragmenting the roadmap before either is confirmed distinct from
> the other): 80 cases failing `vkCreateRenderPass` with
> `VK_ERROR_FORMAT_NOT_SUPPORTED` (likely a `PhysicalDeviceInfo.cpp`
> format-support-reporting gap, not a compiler bug), and roughly 63+ cases with
> a clean but incorrect pixel-comparison `Fail` (likely several distinct
> rendering-correctness gaps in this milestone's own existing "bounded
> payload/output limits reported truthfully" scope, mirroring H6m/H6n/H6o's own
> precedent of leaving in-scope rendering `Fail`s untracked as separate rows)
