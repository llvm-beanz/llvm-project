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

Can you close out L74 from the roadmap or other prerequisites blocking the
L-series milestones?

> **The remaining 66 of roadmap L72(d)'s original 76
> `OpImageQuerySizeLod`/`OpImageQueryLevels` cases -- every shape but `Plain2D`
> (`Array2D`, `Plain1D`, `Array1D`, `Plain3D`, `Cube`, `CubeArray`)** --
> L72(d)'s own fix deliberately scoped its `ImageCalls` builders
> (`createQuerySizeLod2D`/`createQueryLevels`) to emit only a `Plain2D`-shaped
> `v2i32`/`i32` result, so widening `SPIRVResourceLowering.cpp`'s shape gate to
> any other shape without first widening those builders' own result type would
> reproduce the exact `replaceAllUses of value with new value of different
> type!` crash L72(d)'s own fix found and fixed for `Array2D` specifically
> (`textureSize()` against an arrayed shape returns an extra layer-count
> component, e.g. `ivec3` rather than `ivec2`; `Cube`/`CubeArray` likely have
> their own distinct result-shape considerations still needing their own real IR
> reduction to confirm). Not yet started; needs its own per-shape result-type
> design work (likely one new `ImageCalls` builder variant per distinct result
> shape, mirroring L66(f)-L66(j)'s own established per-shape-follow-on precedent
> for `Dref`+`Grad` shadow sampling) before implementation can begin, and should
> probably be split further into its own per-shape rows once that design work
> identifies which shapes share an identical result shape and which need their
> own distinct handling.
