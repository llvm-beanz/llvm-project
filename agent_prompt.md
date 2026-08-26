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

Can you work on milestone H2j?

> **Make a graphics pipeline's fragment shader stage genuinely optional**
> (roadmap H2b's own Deviation): `GraphicsPipeline.cpp`'s
> `translateFixedFunctionState` unconditionally rejects
> `VkGraphicsPipelineCreateInfo::pStages` missing a
> `VK_SHADER_STAGE_FRAGMENT_BIT` entry (`"a graphics pipeline needs both a
> vertex and a fragment stage"`), but Vulkan permits omitting it entirely
> whenever the pipeline's render target has no color attachments
> (depth/stencil-only rendering,
> `VUID-VkGraphicsPipelineCreateInfo-pStages-06894`/neighbors) -- exactly
> `dEQP-VK.multiview.depth_without_fragment_shader`'s own shape (and its
> `dynamic_rendering`/`renderpass2` siblings), still failing after H2b's own fix
> for this exact reason. Needs
> `compileAndValidateStages`/`validateStageInterfaces` to skip fragment-stage
> compilation and cross-stage interface validation when no fragment stage is
> named (only when `Targets.Colors.empty()`, matching the VUID's own condition
> -- a color-attached pipeline still requires one), `GraphicsPipeline`'s
> `FragmentStage` to become a genuinely optional (`nullptr`)
> `shared_ptr<CompiledStage>` rather than always-present, and
> `feme::graphics::executeDraws` to skip its whole fragment-invocation loop
> (`FSSig`/`Varyings`/`FSColors`/`FS.invokeFragments`) when the pipeline has
> none, running only vertex-stage clip/rasterize/early-depth-test with no
> per-fragment shading at all
