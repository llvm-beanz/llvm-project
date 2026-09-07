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

> With both L67(a) and L67(b) now done, `Plain3D` sampling is functionally
> complete except for L67(c) (`ConstOffset`, blocked on the pre-existing
> `isSupportedOffset` `Plain2D`-only restriction, roadmap L33's own scope)
> and L67(d) (integer-format rejection, correct by design, not actionable).
> L67(c) is the same restriction named in roadmap L66(d) from the
> `shaderResourceMinLod` flip's own perspective — fixing `isSupportedOffset`
> to accept a real `ConstOffset` against more shapes than just `Plain2D`
> would be a genuinely cross-cutting change (every shape's own
> `textureoffset*` CTS group depends on it, not just `Plain3D`'s), and is
> probably the highest-value next target across the whole L-series now that
> L66(a)/L67(a)/L67(b) have each independently confirmed it as their own
> respective blocker. L66(c) (the `Dref`+`Grad` shadow-sampling intrinsic
> gap) and L66(e) (the cross-function same-binding crash) remain open and
> untouched this session, unrelated to anything `Plain3D`-specific.


Which seems like the right place to start.

> **L66(a)'s own `Plain3D` ordinary-sampling fix deliberately scoped out
> `Bias`/`MinLodClamp`/`ConstOffset`/`Grad`, each filed here as its own
> follow-on sub-item, mirroring `Sample1D`'s own incremental L52a-\>L61(c)-\>L65
> history**: ~~(a) **`Bias`/`MinLodClamp`** --
> `createSample3D`/`femeCpuImageSample3DV4F32` need a real operand pair added
> (mirroring `createSample1D`'s own roadmap L61(c) extension), confirmed still
> failing this session (`texture.sampler3d_bias_{fixed,float}_fragment`, 2/2
> Fail); also blocks `texturegradclamp`/`textureoffsetclamp`'s own `sampler3d_*`
> cases until `shaderResourceMinLod` can be safely flipped (roadmap L66's own
> still-open scope)~~ (fixed: `createSample3D`/`ImageCallKind::Sample3D` gained
> a real `Bias`/`MinLodClamp` operand pair (widening its argument count from 18
> to 20, inserted between `UseExplicitLod` and `Mask`, mirroring
> `createSample1D`'s own roadmap L61(c) operand ordering exactly),
> `hasOnlySupportedImageUses`'s `HasBias`/`HasMinLodClamp` shape checks now
> accept `Plain3D`, `lowerImageAccesses`'s `Plain3D` branch threads a real
> `MinLodClamp` value (reusing the already-in-scope shared `Bias` extraction)
> via the same `getSampleClampIdx` shape-agnostic helper `Plain1D`/`Array1D`
> already use, and `femeCpuImageSample3DV4F32`'s runtime entry point now takes
> real `Bias`/`MinLodClamp` float parameters threaded into
> `femeRTComputeClampedLod` in place of the previous hardcoded no-op constants.
> New unit-test coverage across all three touched phases: `ImageCallsTest.cpp`'s
> `MatchesSample3DCall` round-trip now asserts real `Bias`/`MinLodClamp` values;
> `SPIRVResourceLoweringTest.cpp`'s now-obsolete `LeavesAPlain3DSampleBiasAlone`
> negative test (asserting `Bias` against `Plain3D` must NOT lower, no longer
> true) was replaced with a new positive
> `LowersSampleBiasClampToPlain3DWithMinLodClamp` test mirroring
> `LowersSampleBiasClampToArray1DWithMinLodClamp`'s own `Array1D` precedent,
> plus a new `LeavesAPlain3DSampleGradAlone` negative test to preserve this
> shape's own negative-test coverage (now naming `Grad`, still correctly
> rejected, rather than `Bias`); `ImageSamplingTest.cpp`'s `Sample3DFn` typedef
> and its 3 existing tests were updated for the new 22-argument runtime
> signature, plus a new `Sample3DBiasSelectsCoarserMipLevel` test (mirroring
> `Sample1DBiasSelectsCoarserMipLevel`) gives real correctness coverage of the
> new operand pair, not just a compile-fix. A new lit-test case
> (`sample_3d_bias_clamp` in `spirv-resource-lowering-image-sample-3d.ll`)
> confirms `llvm.spv.resource.samplebias.clamp` against a `Dim3D` handle now
> lowers successfully. `check-feme`: 2654/2713 pass, 0 fail, 59 unsupported (up
> from 2652/2711, +2 net new tests, 0 regressions). Real CTS, direct re-run of
> `texture.sampler3d_bias_{fixed,float}_fragment`: 2/2 Pass, up from 2/2 Fail. A
> broader `dEQP-VK.glsl.texture_functions.*.sampler3d_*` sweep (502 cases)
> confirms 10 Pass total (up from 8), 264 Fail (down from 266, by exactly these
> 2 newly-passing cases), 228 NotSupported (unchanged) -- no regressions
> anywhere in this shape's own CTS footprint.
> `texturegradclamp`/`textureclamp`/`textureoffsetclamp`'s own `sampler3d_*`
> cases remain confirmed `NotSupported` (`ShaderResourceMinLod feature not
> supported`), unchanged, since `shaderResourceMinLod` itself remains `VK_FALSE`
> (roadmap L66's own still-open scope) -- this row only unblocks *re-measuring*
> that flip experiment once L66(c)/(d)/(e) are also resolved, it does not itself
> flip the bit.); ~~(b) **explicit `Grad` sampling** -- no
> `DUdX`/`DUdY`/`DVdX`/`DVdY`/`DWdX`/`DWdY` operand is threaded from a
> caller-supplied derivative yet (`lowerImageAccesses`'s new `Plain3D` branch
> always synthesizes/zeroes each axis independently, mirroring `Plain1D`'s own
> pre-L65 starting point), confirmed still failing this session
> (`texturegrad.sampler3d_{fixed,float}_{fragment,vertex,compute}`, 6/6 Fail --
> the `_compute` cases fail `vkCreateComputePipelines` itself rather than
> `vkCreateGraphicsPipelines`, the same shape of failure as
> `Plain1D`/`CubeArray`'s own pre-existing `_compute`-stage `Grad` gap)~~
> (fixed: `hasOnlySupportedImageUses`'s `HasGrad` restriction now accepts
> `Plain3D`, and `lowerImageAccesses`'s `Plain3D` branch extracts a real
> per-axis derivative triple from the caller's own `dPdx`/`dPdy` operands (one
> component per axis, since `hasOnlySupportedImageUses`'s own
> `GradDerivativeWidth` check already guarantees these are real 3-wide vectors
> for this non-arrayed shape) in place of a synthesized/zeroed one -- no
> `createSample3D`/runtime signature change needed at all, since roadmap L66(a)
> already gave this builder a real derivative-operand slot for its synthesized
> implicit-LOD case; this row is purely a lowering-phase change reusing that
> same slot for a caller-supplied `Grad` instead, mirroring `Plain1D`'s own
> roadmap L65 precedent exactly. New unit-test coverage:
> `SPIRVResourceLoweringTest.cpp`'s now-obsolete negative
> `LeavesAPlain3DSampleGradAlone` test (asserting `Grad` against `Plain3D` must
> NOT lower, no longer true) was replaced with a new positive
> `LowersSampleGradToPlain3D` test asserting each of the 6 new per-axis
> derivative operands is a real `ExtractElementInst`, plus a fresh
> `LeavesANonZeroTexelOffsetPlain3DSampleAlone` negative test (mirroring
> `LeavesANonZeroTexelOffsetArray2DSampleAlone`'s own `Array2D` precedent) to
> preserve this shape's own negative-test coverage, now naming its real
> remaining gap (`ConstOffset`, sub-item (c) below) rather than `Grad`. A new
> lit-test case (`sample_3d_grad` in
> `spirv-resource-lowering-image-sample-3d.ll`) confirms
> `llvm.spv.resource.samplegrad` against a `Dim3D` handle now lowers
> successfully. `check-feme`: 2655/2714 pass, 0 fail, 59 unsupported (up from
> 2654/2713, +1 net new test, 0 regressions). Real CTS, direct re-run of
> `texturegrad.sampler3d_{fixed,float}_{fragment,vertex,compute}`: **4/6 Pass,
> up from 0/6** (the `_fragment`/`_vertex` cases; the 2 `_compute` cases remain
> `Fail` via `vkCreateComputePipelines` itself, the same pre-existing, unrelated
> gap other compute-stage sampling groups hit -- unaffected by this row). A
> broader `dEQP-VK.glsl.texture_functions.*.sampler3d_*` sweep (502 cases)
> confirms **14 Pass total (up from 10), 260 Fail (down from 264, by exactly
> these 4 newly-passing cases), 228 NotSupported (unchanged)** -- no regressions
> anywhere in this shape's own CTS footprint. `texturegradclamp`'s own
> `sampler3d_*` cases remain confirmed `NotSupported` (`ShaderResourceMinLod
> feature not supported`), unchanged, since `shaderResourceMinLod` itself
> remains `VK_FALSE` (roadmap L66's own still-open scope).); (c)
> **`ConstOffset`** -- blocked by the same pre-existing `isSupportedOffset`
> `Plain2D`-only restriction roadmap L66(d)/L33 already scope, not a
> `Plain3D`-specific gap of its own; (d) **integer-format
> (`isampler3D`/`usampler3D`) filtered sampling** -- correctly rejected by
> design, same as roadmap L66(b), named here for completeness only. Each should
> be scoped and fixed as its own small, independently-committed,
> independently-CTS-measured row rather than attempted together.
