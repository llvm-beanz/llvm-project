---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work the H-series milestones?

The last session suggested the next steps:

1. **H125** (~1-2 hours, real investigation, newly filed this session):
   the divergent-loop reduce-masking gap above -- highest priority
   since it's the direct continuation of this session's own work, and
   the reproducer already exists
   (`offload-test-suite/test/WaveOps/WaveActiveBitXor.convergence.
   test`'s own `ExpectedOut5`/`Out5` shader body, no new repro needed).
   Start by tracing that exact shader through `feme-cpu-linearize`
   (mirroring this session's own `feme-translate`/`feme-opt` chaining
   recipe) to see what `LoopLinearizer` computes for `Masks.Live` inside
   the loop body, and whether `applyStageMasks`'s new masking code (this
   session's own fix) is even reached there with a non-constant mask.
2. **H124d** (~1 hour): `"unhandled opcode 209"` (derivative family),
   ~7 cases, still not started across 3+ sessions now.
3. **H124c** (~1 hour, narrow/mechanical): missing fp16 vector
   resource-load runtime intrinsics, ~2 cases.
4. **H124b** (~1-2 hours): `CBuffer`/`Matrix` `spirv.AccessChain`
   legalization gap, ~10 cases.
5. **H124f** (~1 hour): scalar-only `GLSL.std.450`/`IsNan`/`IsInf`
   vector legalization gaps, 8 cases (`isnan_mat.test` confirmed still
   failing this session with exactly this signature).
6. **H124e** (~2-4+ hours, not one bug): CPU divergence-handling
   cluster, ~11 cases, needs per-case triage first.
7. **`WaveActiveBitAnd.convergence.test`/`WaveActiveBitOr.convergence.
   test`/`WaveActiveMax.test`** (no numeric suffix): confirmed still
   failing this session but were **not** part of H124h's own cited
   list -- likely distinct pre-existing bugs (possibly folding into
   H124e or H124f, not yet confirmed which). Worth a quick triage pass
   before assuming they're H125 duplicates.
8. **H124g** (low priority): confirm `layout.test`'s `FileCheck`
   mismatch is real; consider removing `array_of_matrices.test`'s stale
   `XFAIL: DXC` upstream (in `offload-test-suite`, not this repo).
9. **Still fully pending, now deferred 4+ sessions**:
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`'s
   pre-existing heap corruption -- `valgrind`'s own trace points at
   `buildStageStorage`/`executeDraws` allocating a too-small buffer.
10. Clean up `/tmp/h124h_repro/` (this session's own scratch files, low
    priority, not part of the repo).
