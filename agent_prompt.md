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

Can you close out L72(c) from the roadmap or other prerequisites blocking the
L-series milestones?

> **140 `dEQP-VK.glsl.texture_functions.texelfetch.*` CTS cases against a
> `Plain1D`/`Array1D`/`Plain3D` sampled image still fail with `"...cannot
> normalize into a heap access..."`** -- roadmap L72's own fix deliberately
> scoped its new `llvm.spv.resource.load.level` recognition to
> `Plain2D`/`Array2D` only, mirroring the pre-existing zero-mip
> `getpointer`-based fetch path's own identical, already-documented restriction
> (`hasOnlySupportedImageUses`'s header comment: "this session's own real
> CTS-driven scope is ordinary sampling only... a future row can lift this
> restriction"); a real re-run of roadmap L72's own 1,375-case caselist confirms
> every remaining "cannot normalize" case is exactly a
> `texelfetch.*1d*`/`texelfetch.*3d*` variant. Fixing this needs widening
> `isFetchLevelIntrinsic`'s shape check and `lowerImageAccesses`'s new rewrite
> branch to also dispatch to
> `createLoad1D`/`createLoad1DI32`/`createLoad1DArray`/`createLoad1DArrayI32`/`createLoad3D`/`createLoad3DI32`
> (mirroring the identical dispatch the storage-image fetch path --
> `lowerImageAccesses`'s own `switch (Shape)` a few hundred lines below --
> already has for these same shapes), threading the real `Lod` operand through
> the same way roadmap L72 already does for `Plain2D`/`Array2D`. Not yet
> started.
