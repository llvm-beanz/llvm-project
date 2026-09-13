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

Can you work on H101p or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.all_unordered_and_instance_array.*`'s 26-case
> `spirv.GlobalVariable` legalization failure for multi-member blocks whose
> members are declared out of ascending `Offset` order** (newly characterized
> during H101m's own closing re-triage): every failing case's own decompiled
> SPIR-V interface block has at least one member pair where the *later*-declared
> member has a *smaller* `Offset` than an earlier-declared one (e.g.
> `!spirv.struct<(!spirv.matrix<4 x vector<2xf32>> [12, RelaxedPrecision],
> vector<3xsi32> [0, RelaxedPrecision])>` -- the first declared member sits at
> byte 12, the second at byte 0), consistent with this test family's own name
> (`all_unordered_and_instance_array`, deliberately emitting members out of
> natural layout order) and distinct from every previously-fixed leading-pad
> shape (which only ever involved a *single* out-of-order gap at the very
> front). Not yet triaged -- needs a standalone ground-truth
> `feme-translate`/`feme-opt` repro (mirroring H101m's own technique) of one
> such out-of-order block to determine whether `SPIRVToLLVMPatterns.cpp`'s
> `layOutStructIfOffsetsMatch` (or whichever pass owns struct-layout synthesis)
> assumes monotonically-increasing member offsets somewhere in its own
> layout-matching logic, and if so, whether the fix belongs there or in a new,
> more general "sort members by offset before laying out the LLVM struct" step
