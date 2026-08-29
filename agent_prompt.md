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

Can you complete H6l?

> **`dEQP-VK.mesh_shader.ext.builtin.cull_primitives` reports
> `feme-graphics-validate-stage: 'feme.stage.output.store' ...`
> row/component-out-of-range errors**, alongside the already-diagnosed
> `spirv_var_16` unresolved-global access H6g-b-c's own fix targeted -- newly
> visible only because H6g-b-c wired `ShaderStage::Mesh` into
> `ValidateStagePass::run` for the first time; no other case in a real re-run of
> the full `dEQP-VK.mesh_shader.ext.builtin.*` group (37 cases) hits it, so this
> is narrow, not the same shape as H6g-b-c's own arrayed-block gap. **Updated by
> H6k**: H6k's own fix (folding a mesh entry's constant per-vertex index into
> `Vertex` rather than `Row`, for both plain arrays and builtin interface
> blocks) was the leading candidate this row's own text named, and a real re-run
> of `cull_primitives` confirms it changed this row's own diagnostic shape,
> without closing it: the originally-reported `row N is out of range for element
> {4,5}` is gone, replaced by a mix of `row`/`component is out of range` errors
> spread across elements 1-5 (tallied via a real re-run: components 1/2/3/5 each
> a handful of times, component 4 sixty times, rows 3/4/5 forty-eight times
> combined) -- a real re-run of the same 37-case group both immediately before
> and after H6k's own fix (via `git stash`) confirms the group's own pass/fail
> split is unchanged (22/37 `Failed` either way, not a regression), so this is
> H6k's fix surfacing a different facet of the same underlying
> signature/offset-resolution gap rather than a new bug. Root cause still not
> isolated: needs a real IR reduction of this exact, now-changed case (the same
> technique H6k's own investigation used) to find which of `cull_primitives`'s
> own per-primitive builtin outputs
> (`gl_PrimitivePointIndicesEXT`/`gl_PrimitiveLineIndicesEXT`/`gl_PrimitiveTriangleIndicesEXT`/`gl_CullPrimitiveEXT`-adjacent
> shapes, per its own name) still resolves to the wrong `Row`/component, and
> whether the fix belongs in `resolveOffsetWithinElement`'s own
> per-primitive-block handling or in this shader's own signature reflection
