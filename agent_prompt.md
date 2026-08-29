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

Can you complete H6g-b-c?

> **A mesh entry's unresolved arrayed-builtin-block access -- left unrewritten
> by `H6c-a-a-iii`'s own `resolveOffsetWithinElement` fix (`std::nullopt`,
> "leave for `ValidateStagePass` to diagnose") -- is never actually diagnosed,
> because `ValidateStagePass::run` still does not validate `ShaderStage::Mesh`
> at all (every prior row that touched mesh validation, from `H6a` on, left this
> unreachable/not-yet-wired)**, so the raw, un-canonicalized global-variable
> access survives all the way to `feme::cpu`'s JIT, which then fails with a
> genuinely undefined symbol (confirmed directly:
> `dEQP-VK.mesh_shader.ext.builtin.cull_primitives`, one of this row's own 33
> `vkPipelineConstructionUtil.cpp:176` cases, fails with `JIT session error:
> Symbols not found: [ spirv_var_16 ]`) instead of a clean, diagnosable
> compile-time rejection -- the exact same 9-case-turned-33-case set
> `H6c-a-a-ii`/`H6c-a-a-iii`'s own reports already named (`cull_primitives`,
> `draw_index_in_{mesh,task}`, `local_invocation_{id,index}_in_task`,
> `position`, `primitive_id_glsl`, `work_group_id_in_{mesh,task}`). This is the
> concrete, now-reachable instance of the gap `H6c-a-a-iii`'s own report already
> flagged as a future risk ("not yet reachable... mirroring `TaskPayloadStore`'s
> own 'not yet reachable' precedent")
