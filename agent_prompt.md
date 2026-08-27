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

Can you work on milestone H4i?

> **With H4h's own `SV_Position` relaxation in place, all 24
> `dEQP-VK.tessellation.winding.*` glsl cases now reach real rendering and fail
> at a fourth, later blocker: a systematic front-face/winding-orientation
> mismatch in the rasterized image**, not a pipeline-creation error. A real
> `deqp-vk` run (`dEQP-VK.tessellation.winding.*glsl*`, 24 cases) shows a
> consistent pattern across every `_ccw`/`_cw` pair: where the CTS expects a
> full-viewport quad/triangle of one color, the renderer instead produces the
> *complementary* culled/uncultured result (e.g. `glsl_quads_ccw` gets "Note:
> got 4081 white and 15 red pixels" / "Failure: expected only white pixels
> (full-viewport quad)" while its `_cw` sibling gets "got 0 white and 4096 red
> pixels" for the same expectation, and the `glsl_triangles_*` cases fail with
> "triangle orientation is incorrect" at roughly a 50/50 pixel split) -- the
> shape of a front-face or vertex-winding-order inversion specific to
> tessellated primitives (the domain stage's own emitted vertex order per
> `gl_TessCoord`/`VertexOrderCcw`/`VertexOrderCw`, or how the executor's
> rasterizer classifies a tessellated triangle's front face, likely in
> `PatchPipeline.cpp`/`Executor.cpp`), rather than a per-fragment color or depth
> bug (the two colors present in every failing image are always exactly the two
> the test itself defines for "correctly wound" vs "incorrectly wound"). Root
> cause not yet isolated -- needs its own investigation, likely starting from
> `vktTessellationWindingTests.cpp`'s own pass/fail image classification
> alongside `PatchPipeline.cpp`'s tessellator-output vertex ordering for each of
> `VertexOrderCcw`/`VertexOrderCw` and `Triangles`/`Quads`, and how that
> interacts with `VkPipelineRasterizationStateCreateInfo::frontFace`/`cullMode`
> translation the ordinary (non-tessellated) path already implements
