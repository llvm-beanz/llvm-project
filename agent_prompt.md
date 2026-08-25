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

Can you implement roadmap milestone F16?

> **MLIR's SPIR-V deserializer asserts on `FrexpStruct`/`ModfStruct` result
> types** (`Deserializer::processStructType`, `decoration.has_value()`), found
> incidentally by F15d's own targeted CTS re-run
> (`dEQP-VK.spirv_assembly.instruction.compute.float_controls2.fp32.input_args.frexp_st_*`)
> rather than anything float-controls-specific: these two GLSL.std.450 extended
> instructions return a two-member struct (fraction and exponent) whose members
> this dialect's deserializer apparently expects a member decoration on (likely
> `Offset`, the usual struct-layout one) that a `Function`-storage-class-only
> local struct like this one's result type has no reason to carry, crashing
> (`assert`, not a diagnosed error) rather than importing or cleanly rejecting
> the module. Needs, in order: (1) root-causing exactly which decoration
> `processStructType` unconditionally expects and why a plain (non-interface)
> struct type is exempt from needing it per the SPIR-V spec's own layout rules,
> then (2) a real `spirv.FrexpStruct`/`spirv.ModfStruct`-shaped op
> (`SPIRVGLSLOps.td` or the CL variant) and its `SPIRVToLLVM` conversion
> pattern, if this dialect does not already model either instruction at all --
> unconfirmed as of this row
