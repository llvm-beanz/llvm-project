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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L88 from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real `llvm::Value::~Value` "Uses remain when a value is destroyed!"
> assertion crash in `feme::cpu::LinearizePass`'s `DiamondFlattener`**, split
> out of L85's own closing session: newly found via a speculative
> `VK_SUBGROUP_FEATURE_SHUFFLE_BIT` flag-flip verification run against
> `dEQP-VK.subgroups.shuffle.*` (now that L85's own `GroupNonUniformBallot` fix
> closed the previously-tracked blocker for this bit) -- the very first case in
> the group
> (`dEQP-VK.subgroups.shuffle.compute.subgroupclusteredrotate_bool_constant`)
> aborts the whole `deqp-vk` process outright with `Uses still stuck around
> after Def is destroyed: %live.merge3 = phi i1 [ %live.merge1, ... ], [
> %live.merge1, ... ]`, consistent with this project's own documented precedent
> for this failure class (roadmap C2/H19p/L7m: "a crash silently truncates or
> corrupts a suite run"). A real, `feme`-side bug in `DiamondFlattener`'s own
> nested-diamond live/side-effect-mask `PHINode` merging (not an LLVM core bug,
> unlike L7m's own earlier `DeleteDeadBlocks` false alarm) -- some
> nested-diamond shape leaves an outer merge's `PHINode` referenced by an inner
> one after the outer's own block has already been simplified/erased. Needs its
> own real IR reduction of a minimal nested-diamond-with-shuffle (or, more
> likely, nested-diamond-with-ballot, since shuffle's own CTS verification
> harness is what actually triggers this, per L85's own finding that every
> non-rotate shuffle test calls `subgroupBallot()`) shape to isolate the exact
> merge-ordering bug, before `SHUFFLE_BIT` can be safely advertised
