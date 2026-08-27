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

Can you work on milestone H5e?

> **`vkCreateGraphicsPipelines` accepts `VK_SHADER_STAGE_GEOMETRY_BIT`, and
> `geometryShader`/`maxGeometry*`/`multiviewGeometryShader` are advertised.**
> `GraphicsPipeline.cpp`'s `translateFixedFunctionState` still rejects any stage
> bit besides vertex/fragment/tessellation (the `default:` case's own comment
> already names this row); needs a `GeometryInfo` output parameter mirroring
> `TessControlInfo`/`TessEvalInfo`, `PhysicalDeviceInfo.cpp`'s `geometryShader`
> feature bit and
> `maxGeometryShaderInvocations`/`maxGeometryInputComponents`/`maxGeometryOutputComponents`/`maxGeometryOutputVertices`/`maxGeometryTotalOutputComponents`
> limits (all currently either `VK_FALSE`/0 or absent), and -- now that H2 has
> landed multiview -- `multiviewGeometryShader` in the same
> `VkPhysicalDeviceMultiviewFeatures`/aggregate-1.2-struct H2's own row added
> `multiviewTessellationShader` to (still `VK_FALSE` pending H4's own remaining
> rows). A real `dEQP-VK.geometry.*` run (200 cases) and the standard
> `dEQP-VK.draw.*` regression sample close this row's own measurement, matching
> H4b's own precedent of reporting the real, possibly-partial
> pass/fail/not-supported breakdown rather than assuming "whole group"
