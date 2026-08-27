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

Can you work on milestone H5g?

> **`SPIRVToLLVMPatterns.cpp`'s `StageIOGlobalVariablePattern::matchAndRewrite`
> only attaches `feme.spirv.MemberDecorations` metadata when a stage-IO global's
> pointee type is directly an `mlir::spirv::StructType`, never an
> `mlir::spirv::ArrayType<StructType>`** -- the exact shape a geometry entry's
> `gl_in[]` builtin interface block actually takes (an array of the per-vertex
> block, not the bare block) -- so a real SPIR-V geometry shader's `gl_in`
> global reaches `CanonicalizeStage.cpp` with no member-decoration metadata at
> all today, even after H5b's own `addElements` fix (which peels the outer array
> dimension correctly, but only once metadata is present to peel in front of).
> Needs `StageIOGlobalVariablePattern` to also recognize the
> pointee-is-`ArrayType`-of-`StructType` shape and attach the inner struct's own
> per-member decorations the same way it already does for a bare block, so H5b's
> mechanism has real input to exercise ahead of H5c
