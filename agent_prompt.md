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

Can you work on L7i from the roadmap or other prerequisites blocking the
L-series milestones?

> **Advertise the rest of the Vulkan 1.1
> `VkPhysicalDeviceSubgroupProperties.supportedOperations` feature bits
> `Vote`/`Shuffle`/`ShuffleRelative` now that a real legalization pattern exists
> for at least one op in each** (`GroupNonUniformElect`/`AllEqual` for
> `Basic`/`Vote`, `GroupNonUniformShuffle` for `Shuffle`; `ShuffleRelative`'s
> own `ShuffleUp`/`ShuffleDown` still have no pattern at all and would need one
> first), split out of L7e's own closing session: `PhysicalDeviceInfo.cpp`'s
> `Info.SubgroupSupportedOperations` is hardcoded to
> `VK_SUBGROUP_FEATURE_BASIC_BIT` only, causing
> `dEQP-VK.subgroups.vote.*`/`shuffle.*`'s real CTS cases (confirmed via L7e's
> own re-run, 1,804 cases, 1,676 declined `NotSupported` purely on this
> capability gate) to never even execute against this ICD's now-real
> `Elect`/`AllEqual`/`Shuffle` conversion patterns. Needs: (1) advertising
> `VK_SUBGROUP_FEATURE_VOTE_BIT`/`VK_SUBGROUP_FEATURE_SHUFFLE_BIT` in
> `PhysicalDeviceInfo.cpp` once each op family backing it is confirmed complete
> for every operand type/width the relevant CTS group exercises
> (`dEQP-VK.subgroups.vote.*` alone covers `bool`/`bvec2-4`/`int8_t`-`int64_t`
> and unsigned/float variants --
> `ElectConversionPattern`/`AllEqualConversionPattern` today only handle the
> plain scalar 32-bit-class shapes any known HLSL source reaches, not this full
> type matrix), and (2) a real `deqp-vk` re-run of the newly-unlocked groups to
> confirm they now pass rather than merely stop being skipped
