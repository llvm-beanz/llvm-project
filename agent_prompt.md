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

Can you complete and close out milestone H6g?

> **Triage and close the `dEQP-VK.mesh_shader.*` buckets H6f's own measured run
> found** (mirroring H5e-a through H5e-e's own post-landing triage rows): 235
> `vkCreateGraphicsPipelines` -> `VK_ERROR_INITIALIZATION_FAILED` (real
> mesh/task shader *content* compilation, blocked on H6h's
> `TaskPayloadWorkgroupEXT` lowering and H6i's `CanonicalizeStagePass`
> mesh-stage support -- not this row's own scope, tracked by those two rows
> directly), 68 `vkCreateRenderPass`/1
> `vkGetPhysicalDeviceImageFormatProperties` -> `VK_ERROR_FORMAT_NOT_SUPPORTED`
> (an unrelated render-pass/image format gap, out of mesh shading's own scope
> entirely), and 33 `vkPipelineConstructionUtil.cpp` ->
> `VK_ERROR_INITIALIZATION_FAILED` (graphics-pipeline-library variants of the
> same content-compilation cases as the first bucket, same blocker). Since every
> one of these four buckets is already tracked by an existing row (H6h/H6i) or
> is out of mesh shading's own scope (render-pass/image format support, a
> pre-existing, unrelated gap), this row's own remaining job is narrow: once
> H6h/H6i land and real mesh/task content can compile, re-run
> `dEQP-VK.mesh_shader.*` and confirm the 235+33 content-compilation failures
> clear, then decide whether the format-related 68+1 need their own new roadmap
> row or stay a documented, permanent gap
