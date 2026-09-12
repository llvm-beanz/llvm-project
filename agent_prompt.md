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

Can you work on H99a and H103 or other blocking work to make progress on the
H-series milestones?

The previous session suggested the next steps:

> 1. **Start H103 with the smallest possible repro, not the CTS's own
>    4-overlapping-quad test.** Every failing case in this family
>    conflates two separate questions: (a) does a single blend
>    equation evaluate correctly against a known destination color at
>    all, and (b) does a *second* draw against the same attachment
>    correctly read back what the *first* draw just wrote. Write (or
>    find, if a simpler existing CTS group already does this) a
>    single-quad, single-draw, single-blend-state case first. If it
>    passes, the bug is in (b) -- likely something about how
>    `Executor.cpp` re-reads the destination attachment across
>    sequential draws within one render pass (e.g. a caching/staleness
>    bug, or an incorrect load-op assumption). If it fails, the bug is
>    in (a) -- go straight into `Executor.cpp`'s `blendFactorValue`/
>    `applyBlendOp`/`blendColor` and manually hand-compute one factor
>    combination to find the exact arithmetic divergence.
> 2. **Do not reuse `vktPipelineDualBlendTests.cpp` as a starting
>    point** -- that was a false lead this session. The real source for
>    both the dual-source and plain blend groups' failing case names
>    is `vktPipelineBlendTests.cpp` (`BlendTest`/`DualSourceBlendTest`,
>    `QUAD_COUNT=4`) plus `createOverlappingQuads`/
>    `createOverlappingQuadsDualSource` in `vktPipelineVertexUtil.cpp`.
> 3. **Given H103's likely size (P1, huge case count), budget a full
>    session for it alone** -- this is not a quick follow-up. Consider
>    checking whether other already-passing groups elsewhere in the
>    suite exercise ordinary (non-overlapping, single-draw) blending
>    successfully, which would help bound whether the bug is really in
>    blend-equation math or specifically in the multi-draw-accumulation
>    path.
> 4. H100/H101 (from the original H97 13-crash filing) are still
>    untouched and next in line for the same per-bucket triage.
> 5. Clean up `/tmp/h99a_*` scratch files (qpa logs, decoded PNGs,
>    caselists) -- no longer needed, everything relevant is now
>    captured in the roadmap/CTS-report commits.
