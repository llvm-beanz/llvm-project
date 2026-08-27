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

Can you work on milestone H4b?

> **`vkCreateGraphicsPipelines` still rejects
> `VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT`/`VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT`,
> and `tessellationShader` is still `VK_FALSE`.** `GraphicsPipeline.cpp`'s
> `mapStage`/stage-mask loop (~lines 1157-1174) needs to accept the two bits,
> require exactly one of each when either is present, reject any topology other
> than `VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` for such a pipeline (and reject
> patch-list topology without them), translate
> `VkPipelineTessellationStateCreateInfo::patchControlPoints` into
> `graphics::TessellationState::InputControlPointCount` after validating it
> against `maxTessellationPatchSize`, compile the tessellation modules into the
> hull control-point, patch-constant and domain `CompiledStage`s, and call
> `graphics::GraphicsPipeline::setTessellationStages` -- all of which the
> executor already consumes. `PhysicalDeviceInfo.cpp` then advertises
> `tessellationShader = VK_TRUE` and real `maxTessellation*` limits; note that
> the implementation's own honest ceilings are
> `feme::graphics::MaxPatchControlPoints` (32, `Graphics/Patch.h`) for
> `maxTessellationPatchSize` and `feme::graphics::DefaultMaxTessFactor` (64,
> `Graphics/Tessellator.h`) for `maxTessellationGenerationLevel`, so neither may
> be advertised higher without raising those first. Blocked on H4a: flipping the
> feature bit before a tessellation module can even be reflected would turn the
> group's 1114 honest `NotSupported`s into 1114 `Fail`s
