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

Can you work on H6s or other prerequisites blocking the H-series milestones?

> **`OpEmitMeshTasksEXT` (`spirv.EXT.EmitMeshTasks`), a task entry's own
> mesh-dispatch call (group-count triple plus its `TaskPayloadWorkgroupEXT`
> payload operand), has no `ConvertSPIRVToLLVMPass` conversion pattern at all
> yet** -- found confirming H6q's own fix: `error: failed to legalize operation
> 'spirv.EXT.EmitMeshTasks' that was explicitly marked illegal`, on every
> `with_task_shader`/`with_task_shader_secondary_cmd` variant in the 540-case
> `dEQP-VK.mesh_shader.ext.api.*` group (58 cases) once H6q's own push-constant
> legalization gap no longer blocks them first. Unlike
> `spirv.EXT.SetMeshOutputs` (the mesh stage's own bounded-output-count
> declaration, `SetMeshOutputsEXTConversionPattern`), nothing in
> `SPIRVToLLVMPatterns.cpp` handles this op at all -- `MeshOutputWrapper.cpp`'s
> own file comment already anticipated "`EmitMeshTasksEXT`'s own
> still-uncanonicalized form" as a candidate gap (H6p's own investigation ruled
> it out for that row's specific case, since that case had no task shader at
> all, but it is exactly what every `with_task_shader` case needs). Needs its
> own design: likely a new canonical `feme.stage.*` op (mirroring
> `SetMeshOutputs`'s own precedent) capturing the group-count triple and payload
> pointer, a new `ConvertSPIRVToLLVMPass` pattern converting
> `spirv.EXT.EmitMeshTasks` into it, a `MeshOutputWrapper.cpp` (or a
> task-stage-specific sibling) lowering case for the task stage's own wrapper,
> and whatever CPU-side execution-chaining the task stage's own dispatch of its
> mesh workgroups still needs beyond what already exists for a task-less mesh
> entry. Not yet triaged for how much of the task-stage-to-mesh-stage chaining
> machinery already exists elsewhere versus needs building from scratch
