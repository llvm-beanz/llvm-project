---
model: claude-opus-5
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

Can you work on H29k or other prerequisites blocking the H-series milestones?

> **A real rendering-correctness mismatch in `independent_sets_random`'s own
> IO-buffer/descriptor-contents check** (`vktIndependentSetsUtil.cpp`'s own
> comparison, not a pipeline-creation-time diagnostic), 6 of H29f's own re-run's
> `graphics_library.*` failures (all `mesh_frag.case_1`/`case_1_io_ssbo_first`,
> one pair each across `fast_lib`/`monolithic`/`optimized_lib`). ~~Distinct from
> H29h's own legalization gap (these 6 cases already clear pipeline creation and
> reach real rendering); needs its own real reduction once H29h unblocks enough
> of this sub-group's own cases to make one self-contained~~ (partially done:
> real IR/log reduction found *two* distinct bugs. (1) `Executor.cpp`'s
> `executeDraws` derived its rasterization extent solely from
> color/depth-stencil attachments, leaving it stuck at `0x0` -- and every
> triangle's scissor collapsed to nothing -- for this test's own legal
> zero-attachment-of-any-kind render pass (a fragment stage kept alive purely
> for descriptor side effects); fixed by falling back to `Draw.Scissors`'s own
> union when no attachment supplies an extent, with a new `ExecutorTest.cpp`
> unit test reproducing and confirming the fix. (2) Even with (1) fixed, this
> test's mesh shader emits its two triangles via
> `gl_PrimitiveTriangleIndicesEXT[i] = uvec3(...)`, which has no canonicalized
> `feme.stage.*` op of its own (see `MeshOutputWrapper.h`'s own file comment,
> "left open by this row") and is therefore never routed into
> `FemeMeshArgs::PrimitiveIndices` at all -- every meshlet's own primitive-index
> slots stay at their zero-initialized default regardless of what the shader
> writes, so every triangle after the first reads back as `(vertex 0, vertex 0,
> vertex 0)` (a real reduction confirmed this via direct instrumentation:
> `M.getPrimitiveIndices(P)` returned `(0, 0, 0)` for *both* of this test's two
> triangles, not just the ones that "should" alias 0), producing a degenerate
> zero-area triangle that never rasterizes -- explaining why the fragment
> shader's own `io_ssbo` writes never happened even after (1)'s fix. (2) is the
> same, larger, already-documented-but-unfiled gap now tracked as its own row,
> H29r, and is what actually blocks this row's own closure)
