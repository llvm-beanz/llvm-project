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

Can you work on H9a or other prerequisites blocking the H-series milestones?

> **`vkCreateGraphicsPipelines` rejects a fragment-shader-less pipeline that
> still declares a color attachment, even when nothing ever writes to it** --
> `"a graphics pipeline with color attachments needs a fragment stage"`,
> `GraphicsPipeline.cpp`. Discovered by H9's own real
> `dEQP-VK.query_pool.statistics_query.*` re-run: the suite's `vertex_only`
> pipeline shape (see `VertexShaderTestInstance::createPipeline` in CTS's own
> `vktQueryPoolStatisticsTests.cpp`) legitimately omits the fragment stage while
> still declaring one color attachment with a default (zero) `colorWriteMask`/no
> blend -- spec-legal (no fragment output ever occurs, so nothing is ever
> written), and CTS's own `with_no_color_attachments` sibling variant of the
> exact same pipeline shape already passes, confirming this rejection is
> specific to "has a declared-but-unwritten color attachment", not to "has no
> fragment shader" in general. By far the largest single blocker this row's own
> re-run found (a real per-case tally attributes 4,157
> `vkCreateGraphicsPipelines` `Fail`s, plus a further ~4,752 cascading
> `vkQueueSubmit` failures against pipelines whose creation this same rejection
> silently downgraded to a different, unrelated error path -- see H9b below for
> that second, distinct root cause) -- needs its own real IR reduction of one of
> these exact pipeline-creation calls (the same technique this project's
> H6-series/H8-series chains have used throughout) to confirm precisely which
> spec clause `GraphicsPipeline.cpp`'s own check is over-applying before
> loosening it
