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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H30 or other blocking work to make progress on the H-series
milestones?

> **Mesh and task (amplification) shading**, `VK_EXT_mesh_shader`: G6's stage
> model plus the Vulkan-side pipeline/draw entry points (`vkCmdDrawMeshTasks*`),
> bounded payload/output limits reported truthfully, and the mesh path through
> the same prepared-draw code. Whole `dEQP-VK.mesh_shader` group (partially
> done, broken down below the same way H4/H5 were: H6a closes the execution-mode
> half of "reflect a mesh entry point at all"; H6b-H6i track the remaining
> canonicalization, CPU-lowering, executor-chaining, Vulkan-API-acceptance and
> CTS-triage work an investigation mirroring H5's own found necessary before
> landing; H6g's own CTS triage split further into H6g-a (closed, folded into
> H33) and H6g-b (closed: its own literal "re-run and confirm" ask is done, and
> it found and fixed a real, distinct bug of its own -- a mesh pipeline's
> spec-*ignored* `pVertexInputState`/`pInputAssemblyState` was wrongly rejected
> outright -- but the group's own re-run this uncovered found the 235/33-case
> bucket does *not* fully clear yet, still blocked on three further,
> newly-isolated gaps of its own, tracked as H6g-b-a (closed: root-caused and
> fixed an upstream MLIR SPIR-V-dialect deserialization/serialization gap,
> `PerPrimitiveEXT` decoration unhandled, which had been the dominant single
> cause -- fixing it lets 202 previously-blocked cases progress further,
> uncovering a new dominant blocker in their place, tracked as H6g-b-a-i
> (closed: root-caused and fixed a Vulkan array-stride correctness bug in the
> upstream MLIR SPIR-V dialect's own layout utilities and SPIRVToLLVM
> conversion, unrelated to `PerPrimitiveEXT` specifically -- fixing it lets the
> 80 previously-blocked `spirv.AccessChain` cases progress further, uncovering a
> new dominant blocker in their place, tracked as H6g-b-a-i-a (closed:
> root-caused and fixed a missing `ConvertSPIRVToLLVMPass` conversion pattern
> for `spirv.All`/`spirv.Any` -- fixing it lets the 81 previously-blocked
> `spirv.All` cases progress further, uncovering a new dominant blocker in their
> place, tracked as H6g-b-a-i-a-i (closed: root-caused and fixed a `feme`-local
> `SPIRVResourceLoweringPass` scope gap for direct struct-typed `StorageBuffer`
> handles that `ConvertSPIRVToLLVMPass` had already lowered into
> `llvm.spv.resource.getpointer` + ordinary LLVM `getelementptr` field/array
> accesses -- the pass only accepted flat direct load/store users before, so
> whole functions containing glslang's `readonly buffer { ... }` field
> selections were left untouched and later rejected by `UnsupportedOps`; fixing
> that lets 81 of the 82 formerly-blocked cases progress further, leaving only a
> pre-existing sampled-image/sampler combined-handle rejection and surfacing a
> new dominant blocker in their place, tracked as H6g-b-a-i-a-i-a (closed:
> root-caused and fixed a `feme.cpu.masked.store.*` divergent-vector-value
> consumer gap in `checkVectorDecompositionSupported`/`widenMaskedStore` --
> fixing it lets 68 of the 148 formerly-blocked cases progress further,
> incidentally also closes H6g-b-b (the same code path, mischaracterized there
> as an "aggregate" masked-store value), and surfaces a new dominant blocker in
> their place, tracked as H6g-b-a-i-a-i-b, a divergent vector value used as an
> `fcmp`/`icmp` comparison operand))), and H6g-b-c (closed: wired
> `ShaderStage::Mesh` into `ValidateStagePass::run`, so this unresolved
> arrayed-builtin-block access -- left unrewritten by `H6c-a-a-iii`'s own fix,
> previously reaching the JIT as a genuinely undefined symbol -- is now
> diagnosed cleanly at compile time instead; its own re-run of
> `dEQP-VK.mesh_shader.ext.builtin.*` surfaces one further, narrower blocker of
> its own, tracked as H6l) -- H6g-b-a-i-a-i-c's own re-run of that same bucket
> found its 80 previously-JIT-symbol-blocked cases split evenly between the
> already-tracked H6g-b-c and a new `MeshOutputWrapperPass` catch-all rejection,
> tracked (one level under H30 rather than nested any deeper under H6g-b, per
> the standing instruction against nesting milestone IDs more than one lowercase
> letter deep going forward) as H6g-b-d (closed: root-caused and fixed a
> `lowerMeshStageOps` bug of its own -- its catch-all rejected *any* leftover
> call that was not itself one of the two shapes it lowers, including completely
> unrelated, perfectly ordinary calls a mesh entry legitimately still makes
> (e.g. its own `feme.cpu.resource.load.raw.*` buffer read feeding an output
> store's value), not only a genuinely-unlowered `feme.stage.*` op -- fixing it
> lets the 40 previously-blocked cases progress further, uncovering a new
> `vkQueueSubmit` vertex-output/fragment-input interface-mismatch bug in their
> place, tracked as H6j (closed: root-caused and fixed a `CanonicalizeStage.cpp`
> signature-reflection gap of its own -- a mesh entry's own plain per-vertex
> output global folded its outer per-vertex array dimension into `RowCount`
> instead of peeling it off, disagreeing with the fragment stage's own unarrayed
> input at the same location -- fixing it lets those 40 cases progress further,
> uncovering a new `SIGSEGV`/`SIGABRT` heap-corruption crash inside
> `executeDraws` itself in 32 of them in their place, not yet root-caused,
> tracked as H6k)) -- milestone remains open, depending on H6g-b-a-i-a-i-b, H6l
> and H6k -- all three now closed, but a real full-group re-run this uncovered
> found two more, newly-isolated, generic CPU-lowering gaps of their own,
> tracked as H6n (closed: SubgroupId/NumSubgroups CPU lowering, plus an
> incidental generic divergent-vector-store-operand crash fix) and H6o (closed:
> NumWorkgroups CPU lowering) -- H6o's own re-run found the whole group now runs
> to completion with zero crashes for the first time, uncovering its own new
> dominant Fail bucket, tracked as H6p (closed: root-caused and fixed a genuine
> mesh-shader-input gap, `gl_DrawID`/SPIR-V's `DrawIndex` builtin reaching
> `MeshOutputWrapperPass`'s catch-all with no lowering case -- fixing it
> surfaces two new, distinct blockers in its own newly-unblocked cases' place,
> tracked as H6q (closed: root-caused and fixed a generic
> `SPIRVToLLVMPatterns.cpp` struct-layout gap -- a struct whose first member has
> a nonzero declared offset, real SPIR-V's own shape whenever glslang/spirv-opt
> drops one or more leading, now-unused members from an interface block --
> fixing it clears the push-constant legalization error but surfaces a new,
> later, and substantially larger gap in its own place, tracked as H6s (closed:
> designed and implemented a full `feme.stage.emit_mesh_tasks` canonical op, its
> own `ConvertSPIRVToLLVMPass` conversion pattern, and CPU-side lowering
> chaining a task entry's own mesh-dispatch request into the already-scaffolded
> `Executor.cpp`/`AmplificationDispatchQueue` host-side machinery -- verifying
> it via a real `deqp-vk` re-run found and fixed two further bugs of its own,
> tracked as H6t (closed: a `TaskPayloadWrapper.cpp` catch-all-too-broad bug
> mirroring H6g-b-d's own precedent, plus a task-stage `gl_DrawID` input-load
> lowering gap mirroring H6p's own mesh-stage fix), and surfaced a new,
> distinct, unrelated pipeline-layout-validation gap, tracked as H6u (closed: a
> `GraphicsPipeline.cpp`/`Pipeline.cpp` push-constant coverage check wrongly
> assumed every stage's own accessed push-constant span starts at byte 0 --
> fixed by threading a new `RootConstantMinOffset` field through the reflection
> pipeline so a shared push-constant block's per-stage `layout(offset=N)` is
> honored)) and H31 (a rendering-correctness gap for direct multi-draw mesh
> dispatches) -- milestone remains open, depending on H31)
