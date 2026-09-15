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

1. **H126** (~1 hour, not started): the `si32` prefix-scan type-legality
   gap above. Start by finding wherever `WavePrefixSum`/`WavePrefixProduct`
   gets legalized to see where an `si32` (rather than plain `i32`) type
   survives into an `llvm.call`'s result.
2. **H124b** (~1-2 hours, still not started across several sessions):
   `CBuffer`/`Matrix` `spirv.AccessChain` legalization gap, ~10 cases.
3. **H124d** (~1 hour, still not started): `"unhandled opcode 209"`
   (derivative family, `fwidth`/`ddx`/`ddy`), ~7 cases.
4. **H124f** (~1 hour): scalar-only `GLSL.std.450`/`IsNan`/`IsInf` vector
   legalization gaps, 8 cases.
5. **H124c** (~1 hour, narrow/mechanical): missing fp16 vector
   resource-load runtime intrinsics, ~2 cases.
6. Lower priority, deferred 4+ sessions now: `transform_feedback.fuzz.
   random_geometry.all_instance_array.12`'s pre-existing heap corruption
   -- `valgrind`'s own trace already points at `buildStageStorage`/
   `executeDraws` allocating a too-small buffer.
7. Cleanup: `/tmp/h125_repro/`, `/tmp/h125_test*.ll` (this session's own
   scratch files, not part of the repo).
