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

Can you work on H10d or other prerequisites blocking the H-series milestones?

> **`dEQP-VK.wsi.xcb.swapchain.render.*` fails `vkCreateGraphicsPipelines` with
> `VK_ERROR_INITIALIZATION_FAILED`**, discovered by H10b's own real,
> now-non-crashing `dEQP-VK.wsi.xcb.*` re-run (7 of its 8 real failures, every
> `render.*` sub-case:
> `basic`/`basic2`/`2swapchains`/`2swapchains2`/`10swapchains`/`10swapchains2`).
> The real diagnostic (`FEME_VULKAN_LOG_CREATION_ERRORS`-visible) is an MLIR
> legalization failure: `"failed to legalize operation
> 'spirv.CompositeConstruct' that was explicitly marked illegal"`, building a
> `!spirv.matrix<4 x vector<4xf32>>` from four `vector<4xf32>` column operands
> -- a SPIR-V-to-LLVM lowering gap for matrix-typed `OpCompositeConstruct`,
> entirely unrelated to WSI/swapchain/device-group; any other real CTS case
> whose shader constructs a matrix from column vectors (a common GLSL pattern,
> e.g. `mat4(c0, c1, c2, c3)`) would be expected to hit the same wall. Needs its
> own real IR reduction of one of these exact cases to confirm whether
> `SPIRVToLLVMPatterns.cpp` is simply missing a matrix-result
> `CompositeConstruct` pattern entirely (most likely, given the "explicitly
> marked illegal" framing) or has one that only handles a subset of operand
> shapes
