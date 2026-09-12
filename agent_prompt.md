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

Can you work on H99 or other blocking work to make progress on the H-series
milestones?

> **`pipeline`'s hang and `spirv.Kill` legalization crash** (the single largest
> group in the entire suite, 1,172,229 cases, ~36% of the total suite -- only
> 11,432 of which this session managed to measure): two distinct symptoms seen
> so far -- (1) a genuine hang (100% CPU, zero forward progress for 20-35+
> minutes) on `fast_linked_library.blend.dual_source...b5g5r5a1_unorm_pack16...`
> cases; (2) a `spirv.Kill` legalization failure (`error: failed to legalize
> operation 'spirv.Kill' that was explicitly marked illegal`) on sibling cases
> in the same `fast_linked_library.blend.dual_source` family. Given the group's
> size, this is the single highest-value crash-isolation target of the 13 --
> fixing it (or even just finding a bulk-excludable pattern the way H98 did for
> `image`) would move the largest share of any row here. Not yet triaged | (none
> -- newly found)
