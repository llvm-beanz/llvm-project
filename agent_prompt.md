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

Can you work on H6q or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.mesh_shader.ext.api.draw.*`/`draw_indirect*`'s own
> `with_task_shader`/`with_task_shader_secondary_cmd` variants (58 of the
> 540-case `dEQP-VK.mesh_shader.ext.api.*` group, found re-running H6p's own
> fix) fail `vkCreateGraphicsPipelines` with `VK_ERROR_INITIALIZATION_FAILED`**,
> root cause confirmed directly via `FEME_VULKAN_LOG_CREATION_ERRORS=1 deqp-vk`:
> `error: failed to legalize operation 'spirv.GlobalVariable' that was
> explicitly marked illegal` against a `PushConstant`-storage-class SPIR-V
> global variable (`!spirv.ptr<!spirv.struct<(i32 [12], i32 [16]), Block>,
> PushConstant>`) -- an upstream MLIR SPIR-V-dialect-to-LLVM conversion gap in
> `ConvertSPIRVToLLVMPass`, distinct from H6p's own `feme`-local
> `MeshOutputWrapper.cpp` scope. Confirmed present only in a `with_task_shader`
> variant's *task* stage module (the task/amplification entry that dispatches
> the mesh stage, not the mesh stage itself), and not yet triaged for whether
> the gap is task-stage-specific, push-constant-specific, or a generic "this
> particular struct layout/size" gap -- needs its own IR reduction, mirroring
> the H6g-b/H6j/H6k/H6l/H6n/H6o/H6p technique, to isolate exactly what about
> this push constant block (`i32 [12], i32 [16]`, an 8-byte struct at two large
> byte offsets) the existing `ConvertSPIRVToLLVMPass` `spirv.GlobalVariable`
> lowering pattern does not already handle for other, already-passing
> push-constant-using stages (e.g. the vertex/fragment paths, which do not hit
> this)
