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
letter deep going forward.

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on L26 or other prerequisites blocking the L-series milestones?

> **6 of L22's own 13 cases (`Feature/Textures/{Sample,SampleBias}.test` and
> their `Vk.SampledTexture2D` YAML siblings) now clear `ImageSampleImplicitLod`
> legalization but still fail `vkCreateGraphicsPipelines`, `VkResult = -3`, on a
> distinct, later legalization gap**: `"unsupported raised operation:
> 'llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_0_0_1_0t' is a
> register-bound resource handle the FeMe CPU target cannot normalize into a
> heap access or the root-constant block ... express it as a finite, unambiguous
> traditional binding, bindless ... or the one recognized root-constant
> binding"` (confirmed via `FEME_VULKAN_LOG_CREATION_ERRORS=1`, a real
> diagnostic these cases were silently swallowing without it). Both failing
> cases share a `Texture2D<float4>` bound via a plain, finite `[[vk::binding(N,
> 0)]]` declaration with `MipLevels: 2` in its YAML `OutputProps` -- a genuinely
> traditional, non-bindless, non-unbounded binding by every appearance, so the
> CPU target's own resource-handle-normalization pass is likely misclassifying
> it rather than this being a real unsupported-resource-kind case; needs its own
> real IR reduction (the same `Sample.test` shape, isolating the exact
> resource-handle attributes the normalization pass inspects) to confirm whether
> the multi-mip-level declaration specifically confuses the classifier, or
> something else about this handle shape does
