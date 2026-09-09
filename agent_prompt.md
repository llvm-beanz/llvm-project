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

Can you work on L7q from the roadmap or other prerequisites blocking the
L-series milestones?

> **`_requiredsubgroupsize` compute pipeline creation newly fails "resolved
> group size exceeds maxComputeWorkGroupSize/Invocations" for every
> `dEQP-VK.subgroups.basic.compute.*_requiredsubgroupsize` case in the
> `subgroupmemorybarrier*` family**, split out of L7p's own closing session: now
> that L7p's own specialization-constant patch resolves each case's real,
> pipeline-specialized group size correctly for the first time (rather than
> silently under-allocating), `compileComputePipeline`'s existing
> `maxComputeWorkGroupSize`/`Invocations` validation (`Pipeline.cpp`) rejects
> the resolved value outright for the `_requiredsubgroupsize` variant
> specifically (its non-`_requiredsubgroupsize` twin passes cleanly with the
> same shader source, differing only in the chained
> `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo`). Needs its own real
> re-run with `FEME_VULKAN_LOG_CREATION_ERRORS=1` plus a dump of the actual
> resolved group size and this device's own
> `maxComputeWorkGroupSize`/`maxComputeWorkGroupInvocations` limits to determine
> whether the real bug is in group-size resolution itself (e.g.
> `resolveComputeGroupSize` picking up an override meant for a different
> specialization constant once L7p's patch is applied), in
> `PhysicalDeviceInfo.cpp`'s own advertised limits being too conservative for
> what this device's CPU runtime can genuinely support, or a genuine CTS-side
> requirement this ICD cannot meet at all for a required-subgroup-size compute
> dispatch of this size
