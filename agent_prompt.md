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

1. **H129** (~1-2 hours, real investigation, newly filed this session):
   representable-layout matrix dynamic row/column/scalar-element
   `AccessChain` gap, 418 cases — see this session's own H128 entry
   above for the starting point (`rewriteBlockAccess` in
   `SPIRVToLLVMPatterns.cpp`). Highest priority: same file/area as this
   session's own fix, momentum carries over, and it's the next-biggest
   `dEQP-VK.ubo.*` bucket by far.
2. **H130** (~2-4 hours, needs fresh triage, newly filed this session):
   the four smaller untriaged `dEQP-VK.ubo.*` buckets (76/34/30/16
   cases) — re-run `FEME_VULKAN_LOG_CREATION_ERRORS=1` triage fresh
   after H129 lands, since bucket counts may shift.
3. **H124f** (~2-4+ hours, larger than previously scoped — checked this
   session): `spirv.GL.Normalize`/`spirv.GL.Length`/`spirv.IsNan`/
   `spirv.IsInf` on vector operands have **no legalization pattern at
   all** in this tree (grepped both `feme/lib/Conversion/SPIRVToLLVM/`
   and upstream `mlir/lib/Conversion/SPIRVToLLVM/` — nothing handles
   these ops, scalar or vector). This is not a "vector variant of an
   existing scalar pattern is missing" fix like H124a/H126/H127 turned
   out to be; it needs new patterns written from scratch for all four
   ops (scalar forms too, if those are even currently reached some
   other way — not confirmed). Re-scope before starting: check whether
   scalar `IsNan`/`IsInf` actually pass today via some other path, or
   whether this is a bigger gap than the roadmap row currently implies.
4. **H124d** (large, needs new upstream MLIR SPIR-V dialect ops for
   `OpDPdx`/`OpDPdy`/`OpFwidth`): deprioritized, still its own
   multi-session effort — skip unless someone wants the upstream-MLIR
   piece specifically.
5. Lower priority, deferred 9+ sessions now: `transform_feedback.fuzz.
   random_geometry.all_instance_array.12`'s pre-existing heap
   corruption — `valgrind`'s own trace already points at
   `buildStageStorage`/`executeDraws` allocating a too-small buffer.
