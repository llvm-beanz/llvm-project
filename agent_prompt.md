---
model: claude-sonnet-5
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

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

The last session got stuck.

Can you work on L47 or other prerequisites blocking the L-series milestones?

> **L45's own fix clears the uniform-diamond-inside-a-region diagnostic, but the
> real `task_mesh` cases in the same CTS sweep L45 just fixed the `mesh_only`
> half of now fail on a distinct, later blocker**: `"JIT session error: Symbols
> not found: [ spirv_var_20 ]"` for
> `dEQP-VK.mesh_shader.ext.query.no_queries.lines.no_reset.copy.no_wait.draw.32bit.no_availability.multiple_blocks.task_mesh.inside_rp.single_view.{only_primary,with_secondary}`
> -- an ORC JIT link-time failure (not a diagnosed `vkCreateGraphicsPipelines`
> gap), confirmed via a real pre-JIT IR capture (a temporary
> `FEME_DEBUG_DUMP_PRE_JIT_IR`-gated dump added to
> `feme/lib/Target/CPU/CompiledStage.cpp` right before `JIT->addIRModule`,
> reverted before committing): `@spirv_var_20` is an `external` (never-defined)
> `addrspace(14)` (`TaskPayloadWorkgroupEXT`) global of the payload's own
> declared type, `{ [24 x i32], i32 }`, still referenced directly by a handful
> of per-lane-suffixed (`.lane4.i`/`.lane6.i`/`.lane8.i`/`.lane10.i`)
> `getelementptr`s -- the `SIMDizePass`-widened, post-inlining shape of a masked
> per-lane memory op, never converted into a
> `feme.stage.task.payload.load`/`.store` call at all (both of which properly
> resolve to a real runtime `Payload` pointer via
> `feme::cpu::TaskPayloadWrapper.cpp`'s
> `lowerTaskPayloadStore`/`feme::cpu::MeshOutputWrapper.cpp`'s
> `lowerMeshTaskPayloadLoad` -- neither is the culprit; the call these two lower
> never got created here in the first place). Root-caused to the real CTS
> shader's own source (`vktMeshShaderQueryTestsEXT.cpp`):
> `td.branch[gl_LocalInvocationIndex] = ...`, a **per-invocation dynamic** array
> index into the payload, not the compile-time-constant offset every existing
> task-payload code path assumes -- `feme::graphics::CanonicalizeStagePass`'s
> own `isTaskPayloadGlobal`/`loadTaskPayloadValue`/`storeTaskPayloadValue`
> (`CanonicalizeStage.cpp`) only ever resolves a payload access's address to a
> literal `uint64_t Offset` (see their own doc comments), so a `getelementptr`
> chain with one dynamically-indexed component is never recognized at all,
> leaving the raw `addrspace(14)` load/store on the imported global completely
> untouched all the way through `SIMDizePass`'s widening to JIT link time, where
> the global -- never intended to survive this far, always meant to be fully
> virtualized away into a real `Payload` buffer -- has no definition anywhere
> for the JIT to resolve. Needs its own real design/implementation pass,
> materially bigger than L45's own scope (a genuinely different pipeline phase,
> `CanonicalizeStage.cpp`, not `EntryWrapper.cpp`): (1) a new dynamic-offset
> `feme.stage.task.payload.load`/`.store` call form taking a runtime `Value`
> byte offset (today's form only takes a literal `uint64_t`), (2) teaching
> `CanonicalizeStage.cpp`'s own `BaseAndOffset`-style GEP-chain resolution to
> recognize a chain with exactly one dynamically-indexed array component feeding
> a task-payload access and emit this new call form instead of declining, (3)
> new lowering support for the dynamic-offset call form in both
> `TaskPayloadWrapper.cpp` (store side) and `MeshOutputWrapper.cpp` (load side,
> generalizing `lowerMeshTaskPayloadLoad`'s current
> single-scalar-broadcast-from-a-constant-offset shape to a per-lane
> potentially-different dynamic offset instead), (4) new lit/unit test coverage
> per phase touched, and (5) a real `deqp-vk` re-run of the 2 named `task_mesh`
> cases (and a broader sweep of any other CTS case using a
> per-invocation-indexed task payload) to confirm the fix
