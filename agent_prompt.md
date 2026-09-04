---
model: claude-opus-5
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

Can you work on H29o or other prerequisites blocking the H-series milestones?

> **`vkQueueSubmit` fails a plain (non-library), monolithic
> vertex+geometry+fragment pipeline with `"the geometry stage's declared input
> primitive class does not match the pipeline's topology/tessellation output
> primitive"`**, 217 of H29f's own re-run's `cache.*` failures (the group's
> single dominant cause after H29g), reproducing even on the group's own plain
> `graphics_tests.vertex_stage_geometry_stage_fragment_stage` case -- **not** a
> graphics-pipeline-library-specific gap despite being discovered by this row's
> own GPL-focused re-run. The failing shader's own GLSL `layout(triangles) in;`
> geometry input plainly matches its own pipeline's
> `VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST` (`Executor.cpp`'s own
> `GeomExpectedInput`/`Pipeline.getGeometryState().InputPrimitive` mapping
> tables both look correct by inspection), so the true defect is not yet
> isolated; needs its own real IR/pipeline reduction of the plain `cache.*` case
> (the simplest failing shape) to determine whether the geometry stage's own
> input-primitive attribute is failing to survive from SPIR-V import through to
> this check, or the check's own topology-side value is wrong instead
