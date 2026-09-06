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

Can you work on L56 or other prerequisites blocking the L-series milestones?

> **L53's own real `deqp-vk` sweep of VK-GL-CTS's own dedicated cube-filtering
> combinations group incidentally discovers a distinct, pre-existing (confirmed
> via `git stash` to already fail identically before L53's own change landed)
> trilinear/mipmap cube-filtering bug**:
> `dEQP-VK.texture.filtering.cube.combinations.linear_mipmap_linear.linear.*.*.seamless`
> fails 0/25 (`Fail (Image verification failed)`), both with and without L53's
> own single-level seamless-blending fix -- meaning the bug lies somewhere in
> cube-specific trilinear (cross-mip-level) blending itself, or in mip-level
> selection for a `Cube`/`CubeArray` image specifically, not in the single-level
> bilinear seamless-edge logic L53 just added (which is only ever invoked once
> per mip level inside `femeRTSampleFilteredCube`'s own `Trilinear` branch,
> itself unmodified by L53 beyond its two `femeRTSampleCubeLinearAtLevel` call
> sites). Needs its own real IR/data reduction of one of these 25 failing cases
> (e.g.
> `dEQP-VK.texture.filtering.cube.combinations.linear_mipmap_linear.linear.repeat.repeat.seamless`,
> confirmed failing) to isolate whether the bug is in `femeRTSelectMipLevels`'s
> own cube-specific LOD/mip-level-count computation,
> `femeRTSampleFilteredCube`'s own `Trilinear`/`MipPlan.Frac` blend arithmetic,
> or some other cube-specific mip-chain layout assumption (e.g.
> `femeRTMipExtent`'s own square-face sizing per level) not yet reviewed against
> a real failing case's own captured mip-level/LOD values.
