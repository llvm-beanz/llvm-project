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

Can you work on L42 or other prerequisites blocking the L-series milestones?

> **L40's own fix reaches `dEQP-VK.mesh_shader.ext.misc.payload_read`'s
> verification loop successfully, but the case still fails, now on a distinct,
> later `feme-cpu-linearize` diagnostic**: `"loop at '' has an internal branch
> in '' that does not reach the loop's exit block; unsupported (roadmap
> milestone 6 deviation)"`. Root-caused (via a real captured pre-`LinearizePass`
> IR dump of this exact shader): `DiamondFlattener` runs *before*
> `LoopLinearizer` inside `LinearizePass::run`, and
> `DiamondFlattener::isLoopControlEdge` only recognizes a block's own branch as
> loop control flow when one of its two successors is literally the cycle's
> header (a backedge) or one of `CycleInfo::getExitBlocks`'s own exit blocks --
> it has no visibility into a block whose branch is not *directly* a
> loop-control edge but still feeds, indirectly, into the loop's own real exit
> decision downstream (e.g. by routing a literal constant into a separate merge
> block's own condition `phi`, the exact `Flow`-fusion shape L40's own
> `peelConstantFlowPredecessors` targets). For this real CTS shader (unlike
> L40's own simpler hand-reduced repro), `DiamondFlattener` flattens this loop's
> own plain uniform trip-count check via its own `select`-based masking before
> `LoopLinearizer` ever sees it, replacing the literal-constant incoming value
> L40's own peel logic requires (`isa<ConstantInt>`) with a non-constant
> `select`-derived expression instead -- defeating the peel entirely, so
> `LoopLinearizer` again sees two unclassifiable `OtherCondBrBlocks` entries,
> just a structurally different shape of the same underlying problem L40 fixed
> one instance of. Needs its own real IR reduction of this exact shader's
> captured pre-`LinearizePass` IR (`/tmp/l40_real_pre_linearize.ll`, captured
> but not yet committed anywhere -- a future session should re-capture it via
> the same env-gated dump technique documented in this row,
> `feme/lib/Target/CPU/Pipeline.cpp`, right before `LinearizePass` runs) to
> design a fix: either teach `DiamondFlattener::isLoopControlEdge` to also
> recognize this indirect shape (leaving such a diamond unflattened for
> `LoopLinearizer`'s own peel to handle instead), or teach `LoopLinearizer` to
> see through a `select`-derived (not just a literal constant) condition once it
> can prove -- the same way `peelConstantFlowPredecessors` already does for a
> literal constant -- that a given incoming value is compile-time equivalent to
> one of the loop's own known trip-count outcomes
