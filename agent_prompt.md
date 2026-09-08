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

Can you close out L72(b) from the roadmap or other prerequisites blocking the
L-series milestones?

> **118 `dEQP-VK.glsl.texture_functions.*_compute` CTS cases (the
> `*Offset`-suffixed GLSL builtins, e.g. `texelFetchOffset`/a depth-comparison
> explicit-LOD sample with an offset) fail SPIR-V-to-LLVM legalization
> outright** with `"failed to legalize operation 'spirv.ImageFetch'"` (100
> cases) or `"...'spirv.ImageSampleDrefExplicitLod'"` (18 cases) -- confirmed
> via roadmap L72's own real IR reduction: `ImageFetchLodPattern`/its `Dref`
> counterpart (`SPIRVToLLVMPatterns.cpp`) both explicitly match only a *lone*
> `Lod` image operand (`hasExactImageOperands(..., Lod)`), rejecting any real
> `ConstOffset` combined with `Lod` outright rather than matching-failing it
> through to `llvm.spv.resource.load.level`/its `Dref` counterpart, which (per
> roadmap L72's own fix) already accepts a texel-offset operand today --
> currently always a hardcoded zero, since no caller threads a real one through
> yet. Fixing this needs widening both patterns' own match conditions to accept
> `Lod | ConstOffset`, threading the real (non-zero) offset into the intrinsic
> instead of a synthesized zero, then widening `isFetchLevelIntrinsic`/its
> rewrite branch (`SPIRVResourceLowering.cpp`) to accept a real offset value
> instead of requiring `isZeroOffset`, applying it to `lowerImageAccesses`'s
> existing `X`/`Y`/`Layer` coordinate computation the same way
> `isSupportedOffset` already does for the ordinary-sampling paths. Not yet
> started.
