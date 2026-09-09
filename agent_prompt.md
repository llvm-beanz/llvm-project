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

Can you work on L7l from the roadmap or other prerequisites blocking the
L-series milestones?

> **A missing `spirv.MemoryBarrier` legalization pattern**, split out of L7k's
> own closing session:
> `dEQP-VK.subgroups.basic.compute.subgroupmemorybarrier*`'s own 10 cases
> (`subgroupmemorybarrier`/`subgroupmemorybarrierbuffer`/`subgroupmemorybarrierimage`/`subgroupmemorybarriershared`,
> each with a `_requiredsubgroupsize` twin) now reach real pipeline creation for
> the first time (per L7k's own array-deserialization fix unmasking them), but
> all fail identically with `failed to legalize operation 'spirv.MemoryBarrier'`
> (confirmed via a direct re-run of
> `dEQP-VK.subgroups.basic.compute.subgroupelect`, which shares the same GLSL
> test harness's own generic `subgroupMemoryBarrier()`-family call). This is a
> distinct SPIR-V op from `spirv.ControlBarrier` (already legalized, per the
> `subgroupBarrier()` cases in the same test group passing this
> deserializer/legalization stage without issue) and needs its own new pattern
> converting it to whatever this project's CPU runtime already uses for
> cross-invocation memory ordering (likely a fence/barrier intrinsic call
> mirroring `ControlBarrierConversionPattern`'s own shape, scoped per
> `memory_scope`/`memory_semantics` operand combination the CTS group actually
> exercises)
