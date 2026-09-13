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

Can you work on H101c or other blocking work to make progress on the H-series
milestones?

> **`transform_feedback.fuzz.*instance_array*`'s array-of-block-instances shape
> produces wrong XFB-captured values, not crashes** (newly exposed by H101b's
> own closing regression sweep, once its two heap-corruption crashes were
> fixed): `all_unordered_and_instance_array.28` (`Mismatch at offset 4 expected
> 30 received 72`) and `instance_array_basic_type.mat4.geometry` (`Mismatch at
> offset 0 expected -89 received 3`) both now compile, pipeline-create, and run
> to completion, but capture the wrong bytes. Root cause not yet isolated --
> GLSL's `layout(...) out BlockB { ... } blockB[N];` "array of block instances"
> syntax means each `blockB[k]` is an
> independently-`Location`/`XfbOffset`-addressed captured stream, but
> `CanonicalizeStage.cpp`'s `addElements` plain (non-block) path currently folds
> the whole `[N x Block]` shape into a single flat `RowCount`-many-rows element
> sharing one `Location`/`XfbOffset` pair (fixed, this row's own H101b fix, to
> no longer crash by making `getStageIORowShape`'s row-count accumulation
> correct, but not to assign each array index its own distinct
> `Location`/`XfbOffset`) -- likely needs its own per-array-index decomposition,
> mirroring the per-member decomposition H101b already added for a block's own
> members, but one dimension further out. Not yet triaged -- needs a
> channel-level pixel/byte reduction of a representative case's captured XFB
> buffer (mirroring H88/H93/H99a's own technique) to confirm whether the bug is
> purely in `Location`/`XfbOffset` assignment or also in how far
> `resolveOffsetWithinElement`'s byte-offset arithmetic reaches into the
> flattened element
