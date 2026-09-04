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

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H10f or other prerequisites blocking the H-series milestones?

> **`spirv.MatrixTimesVector` (and, by the same root cause, presumably its whole
> sibling family -- `spirv.VectorTimesMatrix`, `spirv.MatrixTimesMatrix`,
> `spirv.MatrixTimesScalar`, `spirv.Transpose`) is entirely unimplemented**,
> discovered by H10d's own real re-run once its `CompositeConstruct` fix let
> `dEQP-VK.wsi.xcb.swapchain.render.basic`/`basic2`/`2swapchains`/`2swapchains2`
> clear that legalization gate for the first time: `"failed to legalize
> operation 'spirv.MatrixTimesVector' that was explicitly marked illegal: ...
> (!spirv.matrix<4 x vector<4xf32>>, vector<4xf32>) -> vector<4xf32>"` (an
> ordinary `mat4 * vec4` transform, one of the most common shapes in any real
> vertex shader). `grep`-confirming zero matches for any of these five op names
> anywhere in `SPIRVToLLVMPatterns.cpp` before committing to a scope -- this is
> likely a substantial, multi-op family, not a single-op fix, and needs its own
> scoping pass (which of the five ops real CTS coverage actually needs first,
> and whether they share enough lowering shape -- all reduce to a small number
> of dot-products/column-scalings over the matrix's own `!llvm.array` of column
> vectors -- to implement together in one pass) before code lands
