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

Can you work on L7r from the roadmap or other prerequisites blocking the
L-series milestones?

> **`dEQP-VK.subgroups.basic.compute.subgroupmemorybarrierimage` reaches real
> pipeline creation and execution but fails runtime output verification
> ("Failed!")**, split out of L7p's own closing session: now that L7p's own
> specialization-constant patch fixes the `SIGBUS` every case in the
> `subgroupmemorybarrier*` family used to hit, this is the one case in the
> family that still does not pass -- distinct from L7n's own tracked
> `builtin_var` runtime-value gap
> (`gl_SubgroupSize`/`gl_NumSubgroups`/`gl_SubgroupID`), since this shader
> neither declares nor reads any of those builtins; its own distinguishing
> feature within the family is the `r32ui` image (`tempImage`) it actually
> reads/writes (unlike its siblings, which all declare the same binding but only
> `subgroupmemorybarriershared` was previously confirmed to touch its own
> groupshared array instead). Needs its own real output-value reduction (dumping
> the actual image contents this ICD's CPU runtime produces versus what the CTS
> verifier expects) to isolate whether the gap is in this project's own
> image-atomic/coherent-image-access lowering, its
> `spirv.MemoryBarrier`-to-CPU-runtime-barrier mapping's interaction with image
> memory specifically (as opposed to buffer/shared memory, both already
> confirmed working by this same family's other passing cases), or something
> else entirely
