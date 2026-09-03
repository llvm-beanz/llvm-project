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

Can you work on H9b or other prerequisites blocking the H-series milestones?

> **A pipeline with both a vertex and a geometry stage fails at `vkQueueSubmit`
> with `"vertex/domain stage output -> geometry stage input: element 0 and its
> producer element 6 disagree on component/row count or type"`**, discovered by
> the same H9 re-run once the pipeline-statistics suite's own dedicated
> geometry-shader-shaped cases
> (`geometry_shader_invocations`/`geometry_shader_primitives`, plus the
> `_geometry`-suffixed variants of `clipping_invocations`/`clipping_primitives`)
> could run for the first time (previously the entire suite was `NotSupported`,
> per H9's own closure note above, so this stage-IO interface-matching gap was
> never reached by any prior CTS coverage). A real per-case tally attributes
> 4,752 `vkQueueSubmit` `Fail`s to this one diagnostic across every
> geometry-shaped sub-group this suite has
> (`host_query_reset`/`reset_before_copy`/`reset_after_copy`'s own replicated
> copies of the same groups included) -- nothing pipeline-statistics-specific
> about the gap itself, since it fires purely from the vertex-to-geometry
> stage-IO signature comparison, before any query or counter code ever runs; any
> other real CTS case exercising this exact vertex+geometry pipeline shape would
> be expected to hit the same wall. Needs its own real IR reduction of one of
> these exact cases to isolate whether the mismatch is a genuine
> vertex-output/geometry-input signature bug in this suite's own shaders, or a
> `feme`-side element-numbering gap (`element 0` vs. `element 6` suggests an
> indexing/ordering mismatch rather than a type/width one)
