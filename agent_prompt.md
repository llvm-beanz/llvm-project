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

Can you work on L7p from the roadmap or other prerequisites blocking the
L-series milestones?

> **A real, previously-unreached `SIGBUS` crash (apparent jump through a
> poisoned/corrupted function pointer) newly reached by
> `dEQP-VK.subgroups.basic.compute.subgroupmemorybarriershared`/`_requiredsubgroupsize`**,
> split out of L7l's own closing session: now that L7l's own
> `spirv.MemoryBarrier` legalization fix lets this shader's module past
> pipeline-creation legalization for the first time, `deqp-vk` crashes outright
> with `SIGBUS` partway through execution -- a `gdb` backtrace shows the
> crashing PC itself as `0xdca345eadca345ea`, a repeating-byte pattern
> consistent with a poison/uninitialized-memory fill value rather than a real
> code address, suggesting a call through a corrupted or never-initialized
> function pointer somewhere in this shader's own JIT-compiled code or the CPU
> runtime's own dispatch path, rather than an ordinary out-of-bounds memory
> access. This shader is the one `subgroupmemorybarrier*` case that also
> declares an `r32ui` image binding (`tempImage`) alongside the groupshared
> array every sibling case in this family shares, a plausible (but not yet
> confirmed) distinguishing factor. Needs its own careful, isolated reduction
> (likely via a smaller standalone repro exercising the same
> image-binding-plus-groupshared-array-plus-memory-barrier shape, plus a real
> disassembly/`gdb` walk of the actual JIT-compiled function this corrupted
> pointer was meant to call) to root-cause before attempting a fix, entirely
> independent of L7l's own now-closed legalization scope
