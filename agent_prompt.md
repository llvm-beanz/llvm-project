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

1. **H124a** (~2-4 hours, highest-leverage single bucket): the vector
   `spirv.GroupNonUniform*` legalization gap accounts for ~26 of the
   102 remaining `check-hlsl-feme-vk` failures, almost the whole
   `WaveOps/*` cluster. Start by finding the existing pattern that
   *does* legalize a scalar-operand `GroupNonUniform*` op in
   `SPIRVToLLVMPatterns.cpp` and understand why it doesn't generalize
   to a `vector<NxT>` operand already -- likely a per-lane
   scalarization loop is missing, not a fundamentally different
   lowering strategy.
2. **H124d** (~1 hour, second-highest leverage per unit effort): the
   `"unhandled opcode 209"` (derivative-family) gap affects ~7 cases
   across a tight cluster (`fwidth`/`ddx`/`ddy` family) and opcode 209
   is very likely a single missing `OpDPdx`-family case in whatever
   dispatches graphics-stage SPIR-V opcodes -- first find that dispatch
   site (not yet located this session).
3. **H124c** (~1 hour, narrow and mechanical): add the missing
   `feme.cpu.resource.load.raw.{v2f16,v3f16,v4f16,f16}` runtime
   intrinsics/lowering, mirroring whatever pattern the existing f32/i32
   variants already use -- looks self-contained.
4. **H124b** (~1-2 hours): `CBuffer`/`Matrix` `spirv.AccessChain`
   legalization gap, ~10 cases across several matrix-layout/nesting
   shapes -- needs a real investigation into which shapes are and
   aren't covered by existing patterns.
5. **H124f** (~1 hour, may piggyback on H124a's own generalization
   work): scalar-only `GLSL.std.450`/`IsNan`/`IsInf` vector
   legalization gaps, 8 cases.
6. **H124e** (~2-4+ hours, not one bug): the CPU divergence-handling
   cluster, ~11 cases with several distinct diagnostics -- needs
   per-case triage before estimating real scope; likely spans multiple
   future sessions on its own.
7. **H124g** (low priority, ~30-60 min): confirm `layout.test`'s
   `FileCheck` mismatch is a real functional gap vs. a stale test
   expectation; separately, consider whether `array_of_matrices.test`'s
   `XFAIL: DXC` is worth removing upstream (in `offload-test-suite`,
   not this repo).
8. **Still fully pending, now deferred across 2+ sessions**: the
   `transform_feedback.fuzz.random_geometry.all_instance_array.12`
   pre-existing heap corruption -- `valgrind`'s own trace already
   points at `buildStageStorage`/`executeDraws` allocating a too-small
   buffer for a fuzzed multi-member XFB block-array shape, a strong
   head start for whoever picks it up.
9. Clean up `/tmp/h123_repro/`, `/tmp/dup_test.mlir`,
   `/tmp/feme_vk_first_run/`, `/tmp/feme_vk_rerun.log` (this session's
   own scratch files, low priority, not part of the repo).
