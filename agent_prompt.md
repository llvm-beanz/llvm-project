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

Can you work on L7t from the roadmap or other prerequisites blocking the
L-series milestones?

> **Flip `PhysicalDeviceInfo.cpp`'s hardcoded `SubgroupSupportedOperations =
> VK_SUBGROUP_FEATURE_BASIC_BIT` to also advertise `VOTE_BIT`/`SHUFFLE_BIT`**,
> unblocked by L7s's own closing session: every
> `dEQP-VK.subgroups.basic.compute.*` blocker this document has tracked across
> the L7-series (L7k/L7l/L7m/L7n/L7o/L7p/L7q/L7r/L7s) is now resolved, and
> `Vulkan14FeatureInventory.md`'s subgroup-capability audit note's
> pending-blocker list is now empty. Not yet done this session (out of L7s's own
> narrower scope): a real feature-flag flip needs its own broader verification
> pass first -- a full `dEQP-VK.subgroups.*` sweep (not just `basic.compute`,
> but `vote`/`shuffle`-specific test groups and every other shader stage the CTS
> exercises those operations from) to confirm this ICD's
> `feme::cpu::SIMDizePass`/`WaveCalls.cpp` handling of the actual vote/shuffle
> wave-op family (as opposed to the basic/barrier/ballot-adjacent operations
> exercised so far) is itself complete and correct before advertising the
> capability bit, plus a check of whether `VkPhysicalDeviceVulkan11Properties`'
> other subgroup-related fields (`subgroupQuadOperationsInAllStages`, etc.) need
> any accompanying update
