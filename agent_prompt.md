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

Can you work on H32 or other work blocking the the H-series milestones?

> **The optional core 1.0 graphics feature bits.** `PhysicalDeviceInfo.cpp`
> reports exactly three `VkPhysicalDeviceFeatures` bits `VK_TRUE`
> (`robustBufferAccess`, `dualSrcBlend`, `textureCompressionASTC_LDR`, lines
> 349-373); the other ~52 are all `VK_FALSE`. Each is *optional* for a 1.4
> submission, so none blocks a conformance claim — but each is a block of
> mandatory-list cases reported `NotSupported`, and several are cheap on a
> software device (`imageCubeArray`, `independentBlend`, `fillModeNonSolid`,
> `depthClamp`, `depthBiasClamp`, `depthBounds`, `wideLines`/`largePoints` once
> F5's line rasterization lands, `sampleRateShading`, `alphaToOne`, `logicOp`,
> `occlusionQueryPrecise`, `multiDrawIndirect`, `drawIndirectFirstInstance`,
> `vertexPipelineStoresAndAtomics`, `fragmentStoresAndAtomics`,
> `shaderClipDistance`, `shaderCullDistance`, `samplerAnisotropy`,
> `shaderStorageImage*`). Split into sub-rows per cluster when assigned; do
> **not** land as one commit (broken down below the same way H4/H5/H30 were,
> after a full survey of every candidate bit's own real implementation status:
> H7a closes the first, lowest-risk cluster -- five bits the executor/pipeline
> layer already genuinely implements and simply never advertised; H7b-H7j each
> track one remaining cluster that needs real new work first, none of it started
> yet -- milestone remains open, depending on H7b-H7j)
