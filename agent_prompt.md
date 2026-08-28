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

Can you work on milestone H6b?

> **Lift `CanonicalizeStagePass::run`'s stage filter to accept
> `ShaderStage::Mesh`/`ShaderStage::Amplification`**, plus canonicalize a mesh
> entry's bounded per-vertex/per-primitive output-array writes (SPIR-V's
> `PerVertexEXT`/`PerPrimitiveEXT`-decorated `Output` storage-class arrays) and
> a task entry's bounded payload write (`TaskPayloadWorkgroupEXT` storage class)
> into new `feme.stage.*` ops, mirroring how H5b/H5c found and closed geometry's
> own per-vertex dynamic-index gap before lifting its filter. Investigate first,
> the way H5's own investigation (see "Roadmap H5: what H5a found, and why it
> stops here" in VulkanCTSReport.md) found real blockers before writing code --
> do not assume this is a mechanical repeat of H5b/H5c
