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

Can you complete H6g-b-a?

> **`ConvertSPIRVToLLVMPass`/the MLIR SPIR-V dialect deserializer rejects
> `PerPrimitiveEXT` with `error: unhandled Decoration : 'PerPrimitiveEXT'`,
> failing SPIR-V module deserialization outright** before `feme` ever sees the
> module -- the single dominant cause found within H6g-b's own 235-case
> `vkCreateGraphicsPipelines` bucket (202 of 232 cases still failing there hit
> exactly this, per a diagnostic-logged re-run of that bucket alone), and a
> prerequisite for any mesh entry with a real `PerPrimitiveEXT`-decorated
> per-primitive output block (the same shape `H6c-a-a-iii`'s own fix already had
> to reason about downstream, but never gets the chance to reach, since
> deserialization fails first). Root cause not yet isolated (upstream MLIR
> SPIR-V dialect decoration table, or a `feme`-local import shim over it -- not
> yet determined which)
