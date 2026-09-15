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

1. **H129** (~2-4 hours, re-scoped, now highest priority): 236 cases,
   `"failed to legalize operation 'spirv.AccessChain'"` on a
   fully-representable-layout (`ColMajor`, natural `MatrixStride`)
   matrix member of a Block/Uniform struct, attempting a dynamic
   row/column select. Start by reducing one of these 236 cases the
   same way this session reduced `.38` (`deqp-vk
   --deqp-log-decompiled-spirv=enable`, re-import via
   `feme-translate --import-spirv` / `feme-opt
   --feme-convert-spirv-to-llvm`) to see the exact `spirv.AccessChain`
   shape hitting "explicitly marked illegal", and check whether an
   existing whole-matrix-access pattern (H124b/H124i) is close enough
   to extend, or a new pattern is needed.
2. **H130** (~2-4 hours, needs fresh triage): the remaining ~165
   non-H129 cases in the 401-failure set -- 76 dominance errors, ~44
   "cannot normalize" (spread thin, no single common shape found yet),
   16 struct-index-out-of-bounds (check first whether this folds into
   H129 once that's fixed), rest one-offs.
3. **H124f** (~2-4+ hours, still not started across many sessions):
   `spirv.GL.Normalize`/`spirv.GL.Length`/`spirv.IsNan`/`spirv.IsInf`
   on vector operands have no legalization pattern at all (confirmed
   in a prior session, grepped both this tree and upstream MLIR).
4. **H124d** (large, deprioritized): needs new upstream MLIR SPIR-V
   dialect ops for `OpDPdx`/`OpDPdy`/`OpFwidth` -- skip unless someone
   wants the upstream-MLIR piece specifically.
5. Lower priority, deferred 10+ sessions now: `transform_feedback.
   fuzz.random_geometry.all_instance_array.12`'s pre-existing heap
   corruption -- `valgrind`'s own trace already points at
   `buildStageStorage`/`executeDraws` allocating a too-small buffer.
