---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you implement roadmap milestone F8c and close out F8?

> **F8b's own remaining piece: an explicit-sample subpass-input local read, for
> `dynamicRenderingLocalReadMultisampledAttachments`.**
> `buildSubpassInputHeap`'s heap slot for a multisampled attachment is now
> correctly laid out (F8b), but nothing can address a sample other than 0 of it
> yet. Needs, in order: (1) a `Sample` operand on
> `feme::StageOpKind::SubpassLoad` (`StageOps.h`/`StageOps.cpp`), defaulting to
> a constant `0` where absent so single-sample callers are unaffected; (2)
> `SubpassLoadPattern` (`SPIRVToLLVMPatterns.cpp`) reading a real
> `spirv.ImageRead` `Sample` image operand instead of unconditionally rejecting
> one (`hasImageOperands`); (3) `FragmentWrapper.cpp`'s
> `lowerFragmentSubpassLoad` and the CPU runtime's
> `femeRTFetchTexel2D`/`feme.cpu.image.load.2d.v4f32` threading that sample
> index into the texel address `SampleStride` already supports, instead of
> always reading sample 0; (4) a CTS-shaped test binding a multisample color
> (and/or depth/stencil) attachment and reading a specific sample back through
> `subpassLoad`'s explicit-sample form before
> `dynamicRenderingLocalReadMultisampledAttachments` can honestly flip to
> `VK_TRUE`
