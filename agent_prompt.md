---
model: claude-sonnet-5
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
roadmap document (lettered with lowercase letters such as R34a or R34b) to break
down the remaining work for that milestone.

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on milestone H4j?

> **All 24 `dEQP-VK.tessellation.winding.*glsl*` cases still fail even after
> H4i's own domain-origin fix, now uniformly at a small, distinct
> rasterization-precision defect**: exactly 15/4096 stray red pixels along what
> looks like a diagonal/seam pattern for `glsl_quads_*` (coordinates cluster
> near a corner and along a `y == x` diagonal, e.g.
> `(14,14),(15,15),(16,16),(17,17)` and `(45,45),(47,47),(55,55),(57,57)`, plus
> a small run near one corner like `(62,0),(63,0),(62,1),(61,2),(60,3),(59,4)`),
> and a 1-pixel-row-fill boundary discrepancy for `glsl_triangles_*`
> (`verifyResultImage`'s top/bottom-row white-pixel-count check is off by
> exactly one: 64/64 filled where 63 is expected, or 1/64 filled where 0 is
> expected). Only ever appears on the pipeline of each `_ccw`/`_cw` test-case
> pair that is supposed to render visibly (never the one supposed to be fully
> culled), identically across
> `default_domain`/`lower_left_domain`/`upper_left_domain` -- i.e. it does not
> correlate with front-face, winding, or domain origin at all, confirming it is
> a rasterizer tie-break/rounding or tessellator crack-free-bridging-seam issue,
> not a winding bug. Root cause not yet isolated -- candidates to investigate
> first: the tessellator's own "crack-free" boundary-ring-to-inset-core bridging
> (`Tessellator.cpp`'s `appendTriangleLattice`/`tessellateQuad`, whether the
> seam between the two regions is truly watertight at every sample point) and
> the rasterizer's own top-left fill-rule edge-ownership tie-break for two
> triangles sharing an exact edge (`Executor.cpp`'s `edgeFn`/coverage test)
