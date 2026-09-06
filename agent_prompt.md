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

Can you work on L50 or other prerequisites blocking the L-series milestones?

> **L48's own Array2D/Cube/CubeArray depth-comparison sampling slice leaves 5
> distinct, independently-sized gaps still open**, broken down here rather than
> re-attempted together, per this project's own established precedent: (a)
> **`Plain1D`/`Array1D` shadow sampling** (`sampler1d{,array}shadow_*`) has no
> ordinary (non-comparison) sampled-image path on the CPU target at all yet for
> either shape -- needs its own new 1D-sampling infrastructure
> (`ImageCallKind::Sample1D`/`Array1D`-equivalents,
> `createSample1D`/`createSample1DArray`, matching runtime entry points)
> *before* a `SampleCmp1D`/`SampleCmpArray1D` counterpart is even possible, a
> materially bigger prerequisite than any other item in this row; (b) **a `Bias`
> image operand** (`sampler{2d,cube}shadow_bias_fragment` and siblings) fails at
> `ConvertSPIRVToLLVMPass` legalization itself, before
> `SPIRVResourceLowering.cpp` ever sees it --
> `ImageSampleDrefImplicitLodPattern`'s own `SupportedMask`
> (`SPIRVToLLVMPatterns.cpp`) only allows `ConstOffset`/`MinLod`, not `Bias`,
> and no `llvm.spv.resource.samplecmp*` intrinsic form threads an explicit bias
> through today, unlike the ordinary
> `spv_resource_samplebias`/`.samplebias_clamp` family already handled for a
> non-dref sample -- needs its own new intrinsic-lowering design; (c)
> **`samplecmp_clamp`'s own trailing `MinLod` clamp operand** (not yet confirmed
> present in any real failing CTS case measured so far) needs each
> `createSampleCmp*` builder extended with a `MinLodClamp` parameter, mirroring
> `createSample2D`'s own roadmap L26 precedent; (d) **a real, nonzero
> depth-comparison `ConstOffset`** (also not yet confirmed present in a real
> failing case) needs `createSampleCmp2D`/`createSampleCmpArray2D` extended with
> `OffsetX`/`OffsetY` parameters, mirroring the same L26 precedent
> (`Cube`/`CubeArray` have no `ConstOffset` concept in SPIR-V at all, so need no
> equivalent); (e) **the entirely separate LOD-query intrinsics**
> (`spv_resource_calculate_lod`/`.calculate_lod_unclamped`, the 190-case
> `texturequerylod` group, confirmed unaffected at 0/190 by both L48 and this
> row's own re-runs) have no CPU-lowering consumer at all on either the SPIR-V
> or DXIL frontend and need a genuinely new runtime design: a real screen-space
> coordinate derivative (`dFdx`/`dFdy`) threaded through to a query call,
> needing its own design pass to confirm where a per-invocation derivative is
> (or could be made) available in this target's per-lane execution model; (f)
> **a newly-discovered `CubeArray`-shadow rendering bug**
> (`samplercubearrayshadow_fragment`, found by L48's own CTS re-run):
> `vkCreateGraphicsPipelines`/rendering both now succeed, but the rendered image
> mismatches the reference in one localized 32x32-pixel screen-space region
> (pixel bbox `x:[96,127] y:[0,31]` of a 128x128 image) -- confirmed not a
> regression of the underlying cube-face-selection or array-layer-rounding math
> (both independently proven correct by the still-passing ordinary
> `samplercubearray_{fixed,float}_fragment` and shadow
> `samplercubeshadow_fragment` CTS cases, which each reuse one but not both of
> `femeCpuImageSampleCmpCubeArrayF32`'s own code paths), so the bug is specific
> to some interaction between the two only present in the combined
> `CubeArray`+dref-compare path; needs a real IR/pixel-level reduction of this
> exact case to isolate (e.g. a temporary per-pixel debug dump of the selected
> face/layer/mip-level triple compared against the reference renderer's own, the
> same technique this project's H6/H8/H9/L-series chains have used throughout).
> **Update (this session):** performed exactly that real per-sample debug-dump
> reduction (a temporary `FEME_DEBUG_CUBEARRAY_CMP`-gated `fprintf` in
> `femeCpuImageSampleCmpCubeArrayF32`/`femeRTSampleCmp2DAtLevel`, reverted
> before committing) against a real re-run of this exact CTS case, and it
> conclusively **rules out every part of `femeCpuImageSampleCmpCubeArrayF32`'s
> own sampling math as the cause**: (i) face/layer selection
> (`femeRTSelectCubeFace`/`femeRTRoundClampLayer`) matches VK-GL-CTS's own
> `TextureCubeArrayView::selectLayer`/`getCubeArrayFaceIndex` formulas exactly
> (confirmed by independent reading of `tcuTexture.cpp`); (ii) the implicit LOD
> is always clamped to level 0 on *both* sides (this CTS case's own real
> `computeLodFromDerivates` value, hand-derived from its
> `Vec4(-1,-1,1.01,-0.5)`..`Vec4(1,1,1.01,1.5)` coordinate range and the array's
> 64x64 face size, comes out *negative* -- a magnifying, not minifying,
> footprint -- so the real reference renderer clamps to level 0 the same way our
> own hardcoded-`Lod=0` dref-sample path does; this is *not* the same "hardcoded
> Lod=0" limitation named in sub-item (e), since it happens to be a no-op here
> on both sides); (iii) the fetched texel content is bit-for-bit correct at
> every one of 17,408 real logged samples against an independent Python
> re-implementation of VK-GL-CTS's own `fillWithGrid`/`layerCorr`/corner-forcing
> fill algorithm (the only 4 apparent "mismatches" were the deliberate
> forced-identical-corner-texel special case, not real errors); (iv) the
> depth-compare (`femeRTApplyCompare`, `VkCompareOp::LESS` here) is the same
> shared, already-proven-correct helper `samplercubeshadow_fragment` (passing)
> uses. Since every piece of the *sampling* math is now proven correct against
> the real reference formulas, the remaining, now much more precisely scoped,
> candidate is **outside this function entirely**: most likely a
> rasterizer/vertex-attribute-interpolation discrepancy specific to this test's
> 4-wide `texCoord` (whose `w` component doubles as both the array-layer
> selector *and* the depth-compare reference value, a combination no other
> shadow-sampling CTS case in this group exercises), localized to one of the
> full-screen quad's two triangles. This is a materially different,
> cross-cutting scope (attribute interpolation/rasterization, not
> `SPIRVResourceLowering.cpp`/`FeMeRuntimeCPU.c`'s image-sampling code) from
> anything named in this row, so it is broken out as its own row, **L51**,
> below, rather than continuing to file it under this already-large (a)-(f)
> breakdown. Each of (a)-(f) should be scoped and fixed as its own small,
> independently-committed, independently-CTS-measured row rather than attempted
> together, per this project's own established precedent (L26->L33, L45->L47,
> L46->L48)
