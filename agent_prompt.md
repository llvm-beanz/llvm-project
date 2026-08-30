---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you continue working on H7i or any prerequisite work required to complete
the H-series milestones?

> **`samplerAnisotropy`**: `Image.cpp` already stores
> `anisotropyEnable`/`maxAnisotropy` on the sampler descriptor at creation time,
> but no anisotropic filtering logic was found in the actual texture-sampling
> implementation -- sampling still appears isotropic regardless of the stored
> state. Needs the real anisotropic filter kernel wired into the sampling path
> before this can honestly flip (and `maxSamplerAnisotropy`, currently the
> degenerate `1.0f`, would need raising to match) (in progress: a real
> screen-space-derivative-based implicit-LOD computation and a genuine bounded
> multi-tap anisotropic filter are now implemented (`FeMeRuntimeCPU.c`'s
> `femeRTPlanImplicitLod`, `ImageCalls.h`/`.cpp`'s widened `Sample2D` shape,
> both `ResourceLowering.cpp` and `SPIRVResourceLowering.cpp`'s
> `getOrSynthesizeSample2DDerivatives`), covering the plain `sampler2D`
> (`Sample2D`) shape only. Four new unit tests confirm the real behavior end to
> end: `ImageSamplingTest.cpp`'s
> `ImplicitLodWithNoDerivativesReadsBaseLevel`/`ImplicitLodSelectsCoarserMipFromDerivatives`/`AnisotropicSampleDiffersFromIsotropicSample`,
> and `SPIRVResourceLoweringTest.cpp`'s
> `FragmentStageImplicitSampleSynthesizesRealDerivatives`. However, a real
> re-run of `dEQP-VK.texture.filtering_anisotropy.*` found the filter kernel is
> never actually reached: 0/128 pass (64 Fail, 64 `NotSupported` for unrelated
> compute-format gaps), every graphics case failing at
> `vkCreateGraphicsPipelines` with `VK_ERROR_INITIALIZATION_FAILED`. Root-caused
> via `FEME_VULKAN_LOG_CREATION_ERRORS=1` to `SPIRVResourceLoweringPass` not
> recognizing a single combined `OpTypeSampledImage`-style `handlefrombinding`
> call -- the shape glslang actually emits for an ordinary GLSL `uniform
> sampler2D` declaration, as opposed to the separately-declared-then-composed
> image+sampler pattern the pass's existing classification logic expects. A
> broader sweep confirmed this is not specific to anisotropy or this row's own
> changes: `dEQP-VK.texture.filtering.2d.*` (1698 cases) is 0/1698 passing
> overall (258 Fail, all the same graphics-pipeline-creation error; 1440
> `NotSupported`), and classification fails structurally upstream of anything
> this row's own derivative-synthesis code touches, so that code is never
> reached either way -- confirmed pre-existing and unrelated to H7i.
> `samplerAnisotropy` therefore stays `VK_FALSE` (reverted from an earlier
> premature `VK_TRUE`) and `maxSamplerAnisotropy` stays at its degenerate `1.0f`
> floor -- flipping either would be an unverifiable conformance claim. `ninja
> check-feme` (assertions-enabled, ccache build) passes in full, 2054/2113 (59
> pre-existing, unrelated `Unsupported`, 0 `Failed`). Tracked to completion as
> new roadmap follow-on H13d (the `SPIRVResourceLoweringPass`
> combined-sampled-image-handle gap itself). See "Roadmap H7i: measured impact"
> in VulkanCTSReport.md for the full reproduction)
