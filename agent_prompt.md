---
model: claude-sonnet-5
resume: 50bf9c01-6e85-44df-8b7a-5c13ed0b05e1
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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Please investigate and fix the issues tracked by milestone L18:

> **`Feature/StructuredBuffer/packed.test` fails with `'llvm.cond_br' op operand
> #1 must be variadic of LLVM dialect-compatible type, but got 'si32'`**, found
> as an L13a milestone-description correction: this is a real, distinct,
> newly-*exposed* (not newly-caused) gap L13a's own fix did not touch --
> `packed.test` was one of L5's own original 6 crash cases and one of L13's own
> 4 still-graceful-failure cases, but L13a's own fix advances its legalization
> far enough to reach a different failure than either of those rows saw. Root
> cause not yet confirmed, but the shape (a raw, un-type-converted `si32` block
> argument reaching `llvm.cond_br`) closely mirrors L10's own already-fixed
> `spirv.GroupNonUniform*`-integer-reduce `si32` gap (an upstream MLIR pattern
> building an op directly from a raw SPIR-V-signed type rather than running it
> through the type converter first) -- likely the analogous upstream
> `spirv.BranchConditional`-to-`llvm.cond_br` conversion pattern
> (`mlir/lib/Conversion/SPIRVToLLVM/SPIRVToLLVM.cpp`) has the identical bug for
> a block argument's own type, needing a feme-side override at `FeMeBenefit` the
> same way L10's `IntegerGroupNonUniformReducePattern` overrode the analogous
> group-reduce pattern, once confirmed via its own real IR reduction
