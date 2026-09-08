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

Can you close out L76(a) from the roadmap or other prerequisites blocking the
L-series milestones?

> **`RWTexture2DArray` (an arrayed *storage* image, as opposed to L76's own
> *sampled*-image scope) is entirely rejected by pipeline creation**, discovered
> by roadmap L76's own real `Array.*` texture test sweep:
> `Feature/Textures/Array.UnalignedRowPitch.test` (a `[[vk::binding(0,0)]]
> RWTexture2DArray<float> Out : register(u0);` compute shader doing a plain
> `Out[TID] = ...;` store, no unusual operand at all) fails
> `vkCreateComputePipelines` with `"unsupported raised operation:
> 'llvm.spv.resource.handlefrombinding.tspirv.Image_f32_1_2_1_0_2_3t' is a
> register-bound resource handle the FeMe CPU target cannot normalize into a
> heap access..."` -- the same generic `hasOnlySupportedImageUses`-family
> rejection diagnostic prior rows (e.g. L26) have hit for other unsupported
> image shapes/operand combinations, here for a `Dim=2D, Arrayed=1, Sampled=2`
> (storage, non-sampled) image type. No non-array `RWTexture2D` counterpart to
> compare against was needed to confirm this is `Array2D`-specific, since the
> diagnostic itself already names the arrayed image type directly. Not yet
> started; needs its own investigation of whichever `SPIRVResourceLowering.cpp`
> code path currently normalizes a plain (non-arrayed) storage-image handle
> (`isStorageImageIntrinsic`-shaped dispatch, unconfirmed exact name) to
> determine whether it already has *any* `Array2D`-shaped storage-image case at
> all, or whether (like roadmap L74/L75's own `OpImageQuerySizeLod`/similar
> per-shape gaps) it needs a new arrayed-storage-image `ImageCalls` builder
> variant threading a real `Layer` coordinate component through
> `femeRTStore2D`-family runtime calls the same way
> `femeCpuImageSample2DArrayV4F32` already does for the sampled-image side.
