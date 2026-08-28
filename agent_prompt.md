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

Can you work on milestone H5e-c?

> **18 `dEQP-VK.geometry.layered.{1d_array,2d_array}.*` cases
> (`multiple_layers_per_invocation`, `render_to_one`, `render_to_default_layer`,
> `render_to_all`) fail at `vkQueueSubmit` with
> `VK_ERROR_INITIALIZATION_FAILED`**, newly exposed by H5e-a (previously masked
> by the `EmitVertex`/`EndPrimitive` legalization failure). Distinct from
> H5e-b's pipeline-creation-time failure and from the pre-existing `layered.3d`
> image-creation gap (`vk.createImage`, unrelated) and
> `layered.*.fragment_layer` fragment-input gap (`feme-cpu-wrap-fragment`,
> unrelated) -- this bucket is specific to a layered geometry-stage draw's own
> execution, using `gl_Layer` output from the geometry stage rather than just
> varying output-array storage. Root cause not yet isolated
