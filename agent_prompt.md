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

Can you continue working on H13d or any prerequisite work required to complete
the H-series milestones?

> **`SPIRVResourceLoweringPass` does not recognize a single combined
> `OpTypeSampledImage`-style `handlefrombinding` call**, discovered via roadmap
> H7i's own real re-run of
> `dEQP-VK.texture.filtering_anisotropy.*`/`dEQP-VK.texture.filtering.2d.*`
> (0/128, 0/1698 respectively -- every graphics-pipeline case in both groups):
> `vkCreateGraphicsPipelines` fails with `VK_ERROR_INITIALIZATION_FAILED`,
> root-caused via `FEME_VULKAN_LOG_CREATION_ERRORS=1` to `UnsupportedOps.cpp`'s
> generic "register-bound resource handle the FeMe CPU target cannot normalize"
> diagnostic firing on a `handlefrombinding` call whose result type is a literal
> struct combining `spirv.Image` and `spirv.Sampler` in one handle (the shape
> glslang actually emits for an ordinary, idiomatic GLSL `uniform sampler2D`
> declaration) -- `classifySampledImage2DHandle`/`classifySamplerHandle`
> (`SPIRVResourceLowering.cpp`) only recognize a *separately*-declared image
> handle and sampler handle later composed by `insertvalue`/read apart by
> `extractvalue`, not a single call already returning the combined pair.
> Confirmed generic, not anisotropy-specific: an ordinary, unrelated
> `dEQP-VK.texture.filtering.2d.combinations.*` sweep reproduces the identical
> failure. Needs a real investigation into recognizing this combined-handle
> shape directly in `collectNormalizableHandles`/`classifySampledImage2DHandle`
> (most likely: detect a `handlefrombinding` call whose return type is a literal
> struct of exactly `{spirv.Image, spirv.Sampler}`, and treat every direct use
> of it as if it had first been split via a synthetic image/sampler pair,
> without requiring a real `insertvalue`/`extractvalue` round-trip to exist in
> the IR) -- this blocks nearly all real Vulkan CTS graphics-pipeline
> conformance for GLSL-style combined-sampler shaders, not just
> `samplerAnisotropy`
