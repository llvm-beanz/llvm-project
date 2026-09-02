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

Can you work on H6o or other prerequisites blocking the H-series milestones?

> **The same full `dEQP-VK.mesh_shader.*` re-run that found H6n now aborts on a
> new, distinct, generic gap: `LLVM ERROR: Cannot select: intrinsic
> %llvm.spv.num.workgroups`** at
> `dEQP-VK.mesh_shader.ext.misc.many_mesh_work_groups_x` (case 1968/28044) --
> the SPIR-V `NumWorkgroups` builtin (the dispatch's own grid size,
> `vkCmdDrawMeshTasksEXT`'s `groupCountX/Y/Z`) converts cleanly to
> `llvm.spv.num.workgroups` at the SPIR-V-to-LLVM layer, same as
> `SubgroupId`/`NumSubgroups` before H6n's fix, but `feme::cpu::SIMDizePass` has
> no lowering case for it either. Unlike `NumSubgroups` (H6n, a compile-time
> constant derived purely from `hlsl.numthreads`), `NumWorkgroups` is a genuine
> *runtime* dispatch-time value (the CTS case's own name,
> `many_mesh_work_groups_x`, varies it directly), so it cannot simply fold to a
> `ConstantInt` the same way -- needs its own scoping pass to find whether the
> wave-body interface (or an outer wrapper) already threads the dispatch's own
> group-count triple anywhere reachable from `SIMDizePass`, mirroring how
> `WorkgroupId`'s own per-call value (`Env.GroupIDX/Y/Z`) is already threaded,
> or whether a new parameter needs to be added to carry it through. Not yet
> triaged for whether it is mesh/task-specific or a generic compute-adjacent gap
> (no lowering case exists for any stage)
