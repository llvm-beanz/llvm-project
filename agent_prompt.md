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

Can you close out L73 from the roadmap or other prerequisites blocking the
L-series milestones?

> **`OpImageQuerySamples` (SPIR-V opcode 107, 8 of L72(d)'s original 76 cases;
> GLSL's `textureSamples(sampler)`, a multisampled sampled image's own sample
> count) needs the same synthetic-`OpFunctionCall` import-time encoding roadmap
> L72(d) already built for opcodes 103/106, but has its own separate
> prerequisite gap blocking it even after that encoding is wired up**:
> `classifySampledImage2DHandle` in `SPIRVResourceLowering.cpp` rejects every
> multisampled (`Plain2DMS`) *sampled* image handle outright today -- unlike a
> *storage* image, where `Plain2DMS` is already a fully-supported shape (roadmap
> history's own existing `LeavesAMultisampledCubeStorageImageHandleAlone`-style
> precedent notwithstanding, that test is about `Cube`, not `Plain2DMS`, and
> storage-image `Plain2DMS` read/write already works) -- so no handle this
> opcode could ever apply to (a multisampled sampled image, since
> `OpImageQuerySamples` is spec-legal only against a multisampled image) can
> reach this query's own dispatch code at all, regardless of how the opcode
> itself gets lowered. Not yet started; needs its own design investigation into
> whether widening `classifySampledImage2DHandle` to accept `Plain2DMS` is safe
> in isolation (i.e. whether every other sampled-image call site in this file
> already correctly rejects a `Plain2DMS` handle for operations that don't make
> sense against it, such as ordinary filtered sampling) before
> `OpImageQuerySamples` support itself can be added.
