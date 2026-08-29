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

Can you complete H7b-a?

> **Shader-visible cube(array)/2D-array image sampling**:
> `SPIRVResourceLowering.cpp`'s `classifySampledImage2DHandle` and
> `ResourceLowering.cpp`'s `classifyImageHandle` both hard-restrict
> sampled-image handle classification to `Dim=2D`/`Texture2D`, non-arrayed only,
> rejecting every `Cube`/`CubeArray`/`2DArray`-typed shader binding at
> pipeline-creation time (gracefully, per this project's own "unsupported ops
> fail pipeline creation" policy, but before any descriptor lookup is reached).
> Needs, together: (1) widened handle classification accepting
> `Cube`/`CubeArray`/`2DArray` dimensions; (2) a cube-face-selection coordinate
> transform (the classic "major axis" algorithm converting `OpImageSample`'s
> 3-component direction vector, or 4-component with array layer for `CubeArray`,
> into a face index plus 2D UV) not implemented anywhere in the codebase yet;
> (3) a widened CPU runtime texel-fetch primitive (`femeRTFetchTexel2D`,
> `feme/runtime/CPU/FeMeRuntimeCPU.c`) accepting an array-layer/face parameter,
> which it lacks entirely today. H7b's own descriptor-materialization widening
> (byte-correct addressing for these dimensions/layers) is already in place and
> does not need revisiting
