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

Can you work on L68 or other prerequisites blocking the L-series milestones?

The last session reported:

> **Forward-looking notes for the next session**: roadmap L68 (the
> `Lod|ConstOffset` legalization gap in `SPIRVToLLVMPatterns.cpp`) is now
> probably the single highest-value next target across the *entire*
> L-series, since it's confirmed to block every shape's own `_vertex`-stage
> `textureoffset*`/`textureoffsetclamp*` cases uniformly, not just one
> shape's row -- likely a bigger CTS win than any single-shape fix remaining.
> L67(c) (`Plain3D` `ConstOffset`, blocked on the same restriction L33 just
> partially widened) and L66(c)/(e) (the `Dref`+`Grad` shadow-sampling
> intrinsic gap and the cross-function same-binding crash) all remain open
> and untouched this session.


Which seems like the right place to start.

> **A real CTS sweep for roadmap L33 (`Array2D`+`ConstOffset`) surfaced a
> distinct, pre-existing, cross-cutting gap one level earlier in the pipeline
> than anything `SPIRVResourceLowering.cpp` touches**:
> `SPIRVToLLVMPatterns.cpp`'s `ImageSampleExplicitLodPattern` (the MLIR
> SPIR-V-dialect-to-LLVM legalization pattern for
> `spirv.ImageSampleExplicitLod`) only matches a *lone* `Lod` image operand
> (`hasExactImageOperands(..., Lod)` plus an exact 1-operand check) and has no
> handling at all for `Lod` combined with `ConstOffset` -- confirmed via `error:
> failed to legalize operation 'spirv.ImageSampleExplicitLod' ... image_operands
> = #spirv.image_operands<Lod|ConstOffset>`, hit identically for **every shape's
> own `_vertex`-stage `textureoffset*` case** (vertex shaders have no automatic
> derivatives, so GLSL's compiler lowers their `texture()`/`textureOffset()`
> calls to an explicit `Lod`, unlike `_fragment`'s `ImageSampleImplicitLod` path
> `ImageSampleImplicitLodPattern` already handles `ConstOffset` against, per
> roadmap L26) -- confirmed both against `Array2D`
> (`textureoffset.*.sampler2darray_*_vertex`, 5/5 Fail) and `Plain2D`
> (`textureoffset.*.sampler2d_*_vertex`, 5/5 Fail identically), proving this is
> not `Array2D`-specific and was never fixed by L26 either, just never
> CTS-measured against a `_vertex` case until this session's L33 sweep reached
> it. The `llvm.spv.resource.samplelevel` intrinsic this pattern already emits
> takes a trailing `Offset` operand (currently always a hardcoded zero constant)
> -- mirroring `ImageSampleImplicitLodPattern`'s own
> `bitEnumContainsAll(SupportedMask, Actual)`-style combinatorial operand
> handling (`SupportedMask = Lod | ConstOffset` instead of `Bias | ConstOffset |
> MinLod`) should let this pattern thread a real offset through the same
> intrinsic operand slot instead, with no runtime/intrinsic-signature change
> needed at all. Affects every shape's own `_vertex`-stage
> `textureoffset*`/`textureoffsetclamp*` CTS group uniformly, not just
> `Array2D`'s or `Plain2D`'s -- a genuinely high-value, cross-cutting fix once
> picked up
