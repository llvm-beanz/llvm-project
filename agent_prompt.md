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

Can you implement roadmap milestone F15c?

> **`VK_KHR_shader_float_controls2`/`shaderFloatControls2`**: per-instruction
> (rather than only per-entry-point) `FPRoundingMode` decorations -- MLIR's
> `spirv` dialect already models the decoration (`SPIRV_FPRoundingModeAttr`), so
> this is "read a decoration on the individual `spirv.FAdd`/etc. op, not a
> whole-entry-point `spirv.ExecutionMode`, then reuse F15a's
> `ConstrainedRoundTowardZeroPattern`-shaped lowering" rather than a new
> lowering strategy -- plus the extension's own
> `FPFastMathMode`/`FPFastMathDefault` decorations
> (contraction/reassociation/etc. fast-math bits), which are a separate,
> additive mechanism (LLVM's ordinary fast-math flags) rather than another
> constrained-intrinsics consumer. `VK_KHR_shader_float_controls2` does **not**
> add a per-instruction denorm-mode decoration at all (confirmed against the
> SPIR-V spec and LLVM's own `SPIRVSymbolicOperands.td`, which has no
> `FPDenormMode` decoration whatsoever) -- F15's original text assumed one
> existed; F15b's whole-execution-mode `DenormFlushToZero` remains the only way
> to ask for flushed denormals
