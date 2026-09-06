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

Can you work on L55 or other prerequisites blocking the L-series milestones?

> **L53's own real `deqp-vk` validation disproves L51's own root-cause
> hypothesis for `samplercubearrayshadow_fragment`'s 32x32-pixel rendering
> mismatch**: that CTS case's own sampler (`samplerShadowNoMipmap`,
> `vktShaderRenderTextureFunctionTests.cpp`) uses plain `NEAREST`/`NEAREST`
> filtering, not `LINEAR` -- so L53's own new seamless cross-face bilinear
> blending (a `LINEAR`-only fix, matching spec) cannot possibly be the cause,
> and indeed left this exact case's own image-diff bit-for-bit unchanged
> (835.108, before and after). The real root cause is once again genuinely
> unknown and needs a fresh investigation from scratch, most likely following
> the same real per-sample debug-dump reduction technique L50/L51 already used
> once for this same case's own varying-interpolation hypothesis (already
> disproven; the fed-in `v_texCoord` was confirmed bit-for-bit correct against
> VK-GL-CTS's own analytic reference formula). Candidate hypotheses not yet
> investigated: (a) a `NEAREST`-specific bug in `femeRTSelectCubeFace`'s own
> major-axis tie-breaking or face-local UV-to-integer-texel rounding, distinct
> from L53's own `LINEAR`-only bilinear-tap logic; (b) a bug specific to the
> depth-comparison (`CmpCubeArray`) path's own layer/Dref extraction from the
> dual-purpose `v_texCoord.w` component, not yet directly probed with real
> captured values on this specific mismatched pixel range; (c) a
> rasterizer/interpolation edge case not covered by L51's own
> single-pixel-center probe (e.g. multisampling, or a provoking-vertex
> convention difference) despite L51's own broader 1,088-fragment capture
> finding no discrepancy. Needs its own real IR/data reduction of this exact
> case before attempting a fix, per this project's own established
> L6-series/H6-series/H8-series/H9-series/L45-series precedent, rather than
> another unvalidated hypothesis.
