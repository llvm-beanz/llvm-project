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

Can you work on L51 or other prerequisites blocking the L-series milestones?

> **L50 sub-item (f)'s own real per-sample debug-dump reduction (this session)
> rules out `femeCpuImageSampleCmpCubeArrayF32`'s own face/layer selection, LOD
> clamping, texel-fetch content, and depth-compare application as the cause of
> `samplercubearrayshadow_fragment`'s localized 32x32-pixel (`x:[96,127]
> y:[0,31]` of 128x128) rendering mismatch** -- every one of those four pieces
> was independently proven correct against VK-GL-CTS's own reference formulas
> (`tcuTexture.cpp`'s
> `TextureCubeArrayView::selectLayer`/`getCubeArrayFaceIndex`, a hand-derived
> real `computeLodFromDerivates` value, an independent Python re-implementation
> of `fillWithGrid`/`layerCorr`/corner-forcing, and the already-proven-correct
> `femeRTApplyCompare` shared with the passing `samplercubeshadow_fragment`
> case). The remaining, now precisely-scoped candidate lies **outside the
> image-sampling runtime entirely**: this CTS case's 4-wide `texCoord` attribute
> has its `w` component doing double duty as both the array-layer selector and
> the depth-compare reference value (`texture(u_sampler, v_texCoord,
> v_texCoord.w)`) -- a combination unique to this one case in the `*shadow*`
> group -- and the mismatch's exact alignment with the full-screen quad's own
> triangle-diagonal split (the mismatch region is a full quadrant, not a thin
> boundary strip) makes a rasterizer/vertex-attribute-interpolation discrepancy
> (not a `SPIRVResourceLowering.cpp`/`FeMeRuntimeCPU.c` image-sampling bug) the
> most likely remaining explanation. Needs: (1) a real, targeted reduction of
> the rasterizer's own 4-component attribute interpolation specifically at/near
> the quad's triangle-diagonal boundary (e.g. a temporary per-fragment debug
> dump of the interpolated `texCoord.w` alongside its exact
> barycentric/triangle-index provenance, mirroring this session's own
> `FEME_DEBUG_CUBEARRAY_CMP` technique but placed in the
> rasterizer/attribute-interpolation code instead of the image-sampling runtime)
> to confirm whether the interpolated `w` value itself is wrong in the
> mismatched region, or whether the true root cause lies elsewhere still; (2)
> once isolated, a real fix plus new unit test coverage for whichever phase is
> actually at fault (rasterizer attribute interpolation, if confirmed, has no
> existing per-phase unit test suite in `feme/unittests/` today and would need
> one added); (3) a real `deqp-vk` re-run of `samplercubearrayshadow_fragment`
> (and a broader sweep of any other CTS case combining a 4-wide
> dynamically-dual-purposed vertex attribute with a full-screen quad, to check
> whether this is an isolated case or a wider-reaching rasterizer gap) to
> confirm the fix. This is a materially different, cross-cutting scope from L50
> sub-items (a)-(e) (all real `SPIRVResourceLowering.cpp`/runtime image-sampling
> gaps), so it is filed as its own row rather than folded into that breakdown.
