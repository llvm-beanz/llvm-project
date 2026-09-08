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

Can you close out L75 from the roadmap or other prerequisites blocking the
L-series milestones?

> **The `OpImageQuerySizeLod` half of roadmap L74's original per-shape scope,
> deliberately left untouched by L74's own fix**: every shape but `Plain2D`
> (`Array2D`, `Plain1D`, `Array1D`, `Plain3D`, `Cube`, `CubeArray`) still needs
> its own new `ImageCalls` builder before `SPIRVResourceLowering.cpp`'s
> `isQuerySizeLodCall` shape gate can be widened, since (unlike
> `OpImageQueryLevels`, which L74 confirmed is shape-independent and needed zero
> builder changes) `OpImageQuerySizeLod`'s result genuinely varies in component
> count by shape per GLSL's own `textureSize(sampler, lod)` overload spec:
> scalar `i32` for `Plain1D`; `v2i32` for `Array1D`/`Plain2D`/`Cube`; `v3i32`
> for `Array2D`/`Plain3D`/`CubeArray`. A real CTS re-run of L74's own 68-case
> caselist found 24 `query.texturesize.*_compute` cases remain in this bucket
> (`isampler1d(array)?`/`isampler3d`/`isamplercube(array)?`/`isamplercubeshadow`/`sampler1d(array)?(shadow)?(_fixed\|_float)?`/`sampler2darray(shadow)?(_fixed\|_float)?`/`sampler3d(_fixed\|_float)?`/`samplercube(array)?(shadow)?(_fixed\|_float)?`/`usampler1d(array)?`/`usampler2darray`/`usampler3d`/`usamplercube(array)?`,
> mirrored across signed/unsigned/float sampler variants). Not yet started;
> needs its own per-shape result-width design work (likely one new `ImageCalls`
> builder variant per distinct result width -- `v2i32` for `Array1D`/`Cube`,
> `v3i32` for `Array2D`/`Plain3D`/`CubeArray`, scalar for `Plain1D` -- grouping
> shapes that already share an identical result shape rather than one builder
> per individual shape) before implementation can begin. Also still needs its
> own confirmation of how `ArrayLayers` is populated for a `CubeArray` view
> specifically (whether it already stores the real face-count-inclusive layer
> count or an already-divided-by-6 array-slice count) before a
> `CubeArray`-shaped builder can be implemented correctly.
