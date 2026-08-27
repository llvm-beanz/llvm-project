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

Can you work on milestone H5e-b?

> **21 `dEQP-VK.geometry.*` cases fail `vkCreateGraphicsPipelines` with
> `VK_ERROR_INITIALIZATION_FAILED` and no diagnostic printed at all**
> (`builtin_variable.in_block.primitive_id_in`/`primitive_id_in_restarted`,
> `input.basic_primitive.{line_strip,line_strip_adjacency,triangle_fan}`,
> `input.triangle_strip_adjacency.vertex_count_*`,
> `emit.{line_strip,points,triangle_strip}_emit_0_end_0`) -- exactly H5e's own
> flagged bucket, unchanged in composition and count now that H5e-a's
> `EmitVertex`/`EndPrimitive` noise is gone. Root cause not yet isolated at all:
> needs bisecting which of `GraphicsPipeline.cpp`'s geometry-stage acceptance
> checks, `GeometryWrapperPass`, or a still-missing
> input-primitive-class/degenerate-zero-emit-shader code path silently rejects
> pipeline creation with no error text reaching the log
