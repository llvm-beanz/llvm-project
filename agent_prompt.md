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
roadmap document to break down the remaining work for that milestone.

During the H6 milestone breakdowns things have gone a little crazy with nesting
letters in strange ways. Please avoid nesting milestones more than one lowercase
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done.

# Request

Can you work on H70 or other blocking work to make progress on the H-series
milestones?

> **`dEQP-VK.mesh_shader.ext.{builtin,misc,properties,smoke,synchronization}`
> failures found by H31's own closing full-group re-run (7 + 36 + 14 + 17 + 81 =
> 155 cases)**: mostly `vkCreateGraphicsPipelines`/`vkQueueSubmit` failures
> (`VK_ERROR_INITIALIZATION_FAILED`) rather than pixel-comparison mismatches,
> confirmed pre-existing (reproduces identically against the pre-H31-fix build).
> Not yet triaged -- several may already be covered by other tracked, unrelated
> gaps (the `smoke.fast_lib.*`/several `builtin.layer*` failures' own
> `vkPipelineConstructionUtil.cpp` error site looks like it may overlap
> H34/H48's own `VK_EXT_graphics_pipeline_library` scope; `synchronization.*`'s
> `vkQueueSubmit` failures look like a distinct, not-yet-identified
> synchronization-feature gap), but none of the five buckets has had a real
> reduction done yet to confirm which milestone (existing or new) actually owns
> each. Needs its own per-bucket triage pass before further breakdown
