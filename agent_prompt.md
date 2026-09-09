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

Can you work on L7d from the roadmap or other prerequisites blocking the
L-series milestones?

> **`spirv.ImageDrefGather` has no conversion pattern**, split out of L7's own
> original filing text (this op is a distinct SPIR-V core opcode, not a
> GLSL.std.450 extended-instruction-set builtin, so is tracked separately from
> L7c's own cluster despite being adjacent in L7's original prose). UPDATE
> (L7b's own closing investigation this session): a real, concrete repro is now
> confirmed -- offload-test-suite's own
> `Vk.SampledTexture2D.GatherCmp.test.yaml` (a real dxc-compiled HLSL
> `Texture2D::GatherCmp()` call, confirmed via a real `check-hlsl-feme-vk`
> re-run) fails with exactly `failed to legalize operation
> 'spirv.ImageDrefGather'`, confirming this row's own original filing (the op
> itself already exists in upstream MLIR -- confirmed present in
> `SPIRVBase.td`/`ImageOps.cpp` -- but
> `feme/lib/Conversion/SPIRVToLLVM/SPIRVToLLVMPatterns.cpp` has no legalization
> pattern for it at all, no `ImageDrefGatherPattern` class, unlike the extensive
> family of `ImageSample*Pattern`/`ImageSampleDref*Pattern` classes that do
> exist). Needs its own scoping pass: likely a new `ImageDrefGatherPattern`
> (mirroring `ImageSampleDrefImplicitLodPattern`'s own shape) converting to a
> new `llvm.spv.resource.gathercmp`-style intrinsic, plus a corresponding new
> `hasOnlySupportedImageUses`/`lowerImageAccesses` case in
> `SPIRVResourceLowering.cpp` and a new `feme.cpu.image.gathercmp.*` CPU runtime
> helper (`ImageCalls.h`) -- a real new codegen surface this project has never
> needed before (gather returns 4 texels' worth of one component each, a
> different intrinsic shape than an ordinary filtered sample). Note this is
> distinct from `spirv.ImageSampleDrefImplicitLodOp`/`ImageSampleDrefGradOp` et
> al., which this file already has patterns for
