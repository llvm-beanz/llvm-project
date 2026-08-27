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

Can you work on milestone H5e-a?

> **`ConvertSPIRVToLLVMPass`/`SPIRVToLLVMPatterns` have no conversion pattern
> for SPIR-V's `spirv.EmitVertex`/`spirv.EndPrimitive` ops at all** (confirmed
> absent by grepping the whole `lib/`/`include/` tree for
> `EmitVertexOp`/`EndPrimitiveOp`), the root cause behind 122 of H5e's own
> measured 167 `dEQP-VK.geometry.*` failures (`error: failed to legalize
> operation 'spirv.EmitVertex'`/`'spirv.EndPrimitive'` -- virtually every real
> GLSL geometry shader calls both, since without them a geometry stage can emit
> no output vertices at all). Needs a new pattern turning each op into a call to
> the same `feme.stage.stream.emit`/`feme.stage.stream.cut` intrinsics
> `GeometryWrapperPass::lowerGeometryStreamEmit`/`lowerGeometryStreamCut` (built
> under G5, already fully implemented and tested) already know how to lower,
> mirroring how every other stage-IO SPIR-V op already routes through a
> `feme.stage.*` intrinsic rather than a bespoke LLVM IR shape. A real
> `dEQP-VK.geometry.*` re-run after this row lands should re-triage the
> remaining, still-unexplained ~21-case "silent
> `VK_ERROR_INITIALIZATION_FAILED`, no diagnostic emitted" bucket H5e's own
> report flagged (degenerate zero-emit shaders and the
> `triangle_strip_adjacency`/`basic_primitive` input-primitive-class shapes)
> once the dominant EmitVertex/EndPrimitive noise is gone, splitting out further
> lettered rows for whatever remains
