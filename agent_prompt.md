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

Can you work on L67 or other prerequisites blocking the L-series milestones?

The last session reported:

> Roadmap L67(a) (`Bias`/`MinLodClamp`) looks like the natural next step,
> since it directly unblocks re-measuring the `shaderResourceMinLod` flip
> experiment's own `sampler3d_*` cases under `texturegradclamp`/
> `textureoffsetclamp` (roadmap L66's own still-open scope) the same way
> `Array2D`'s equivalent fix did earlier in this chain. L67(b) (`Grad`) is
> independent and could be done in either order. L66(c)/(d)/(e) (the
> `Dref`+`Grad` shadow-sampling intrinsic gap, the `isSupportedOffset`
> `Plain2D`-only restriction, and the cross-function same-binding crash) all
> remain open from the prior session and are still unrelated to anything this
> session touched.

Which seems like the right place to start.

> **L66(a)'s own `Plain3D` ordinary-sampling fix deliberately scoped out
> `Bias`/`MinLodClamp`/`ConstOffset`/`Grad`, each filed here as its own
> follow-on sub-item, mirroring `Sample1D`'s own incremental L52a-\>L61(c)-\>L65
> history**: (a) **`Bias`/`MinLodClamp`** --
> `createSample3D`/`femeCpuImageSample3DV4F32` need a real operand pair added
> (mirroring `createSample1D`'s own roadmap L61(c) extension), confirmed still
> failing this session (`texture.sampler3d_bias_{fixed,float}_fragment`, 2/2
> Fail); also blocks `texturegradclamp`/`textureoffsetclamp`'s own `sampler3d_*`
> cases until `shaderResourceMinLod` can be safely flipped (roadmap L66's own
> still-open scope); (b) **explicit `Grad` sampling** -- no
> `DUdX`/`DUdY`/`DVdX`/`DVdY`/`DWdX`/`DWdY` operand is threaded from a
> caller-supplied derivative yet (`lowerImageAccesses`'s new `Plain3D` branch
> always synthesizes/zeroes each axis independently, mirroring `Plain1D`'s own
> pre-L65 starting point), confirmed still failing this session
> (`texturegrad.sampler3d_{fixed,float}_{fragment,vertex,compute}`, 6/6 Fail --
> the `_compute` cases fail `vkCreateComputePipelines` itself rather than
> `vkCreateGraphicsPipelines`, the same shape of failure as
> `Plain1D`/`CubeArray`'s own pre-existing `_compute`-stage `Grad` gap); (c)
> **`ConstOffset`** -- blocked by the same pre-existing `isSupportedOffset`
> `Plain2D`-only restriction roadmap L66(d)/L33 already scope, not a
> `Plain3D`-specific gap of its own; (d) **integer-format
> (`isampler3D`/`usampler3D`) filtered sampling** -- correctly rejected by
> design, same as roadmap L66(b), named here for completeness only. Each should
> be scoped and fixed as its own small, independently-committed,
> independently-CTS-measured row rather than attempted together.
