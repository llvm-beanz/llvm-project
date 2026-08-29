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

Can you complete H6g-b-d?

> **`feme::cpu::MeshOutputWrapperPass::lowerMeshStageOps` diagnoses
> "feme-cpu-wrap-mesh-output: unexpected stage op left for the mesh output
> wrapper"** on 40 of the 80 cases in the same
> `dEQP-VK.mesh_shader.ext.in_out.*` bucket H6g-b-a-i-a-i-c's own real-ICD
> re-run found -- discovered while confirming that row's fix: after adding the
> missing `feme.cpu.resource.load.raw.v2f32`/`v3f32`/`v2i32`/`v3i32`/`v4i32`
> runtime overloads and re-running the full 560-case bucket, the 80
> previously-JIT-symbol-blocked cases split evenly, 40 now hitting the
> already-tracked H6g-b-c `spirv_var_NN` gap and 40 hitting this new blocker
> instead. Root cause not yet isolated: `lowerMeshStageOps`
> (`MeshOutputWrapper.cpp`) only accepts
> `isMaskedOutputStoreCall`/`isMaskedSetMeshOutputsCall` as the two lowerable
> shapes once a mesh entry point uses any `feme.stage.*`/masked-output op at all
> (`isStageOpCall(*CI) \|\| isMaskedOutputStoreCall(*CI) \|\|
> isMaskedSetMeshOutputsCall(*CI)` gates entry into the function at all); its
> closing catch-all `F.getContext().emitError(CI, ...)` fires on every surviving
> call that is neither of those two -- an unmasked (not yet lowered by
> `Linearize.cpp`'s `applyStageMasks`) `feme.stage.output.store`, or some other
> `StageOpKind` a mesh entry should never legally contain (`InputLoad`, an
> interpolation op, etc., per the function's own comment "a mesh entry point has
> no ordinary stage-IO input to read"), reaching this pass unexpectedly. Needs a
> real failing shader/IR reduction (the same one-off
> diagnostic-dump-and-single-case-rerun technique H6g-b-a-i-a-i-a/-b used) to
> find which `feme.stage.*` op survives unlowered and why, before deciding
> whether the fix belongs in `MeshOutputWrapperPass` itself (accept a new shape)
> or upstream in whichever pass was supposed to have already lowered/masked it
