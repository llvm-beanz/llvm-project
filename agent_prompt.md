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

Can you work on H29g or other prerequisites blocking the H-series milestones?

> **`feme-cpu-wrap-hull: control-point phase only supports a control point
> reading its own input control point's attributes`**, the real, distinct
> limitation H29e's own fix exposed underneath its `cache.*` re-run's
> tessellation-stage cases (e.g.
> `pipeline_from_incomplete_get_data.vertex_stage_tessellation_control_stage_tessellation_evaluation_stage_fragment_stage`,
> still `Failed` post-H29e, now on this diagnostic instead): `HullWrapper.cpp`'s
> existing (and, per `HullWrapperTest.DiagnosesCrossControlPointInputLoad`,
> deliberately diagnosed rather than silently miscompiled) cross-control-point
> input-read restriction -- a control point reading another control point's own
> attributes (not just its own, self-indexed ones) needs a real design for how
> the CPU-emulated control-point phase, which currently processes one
> invocation's own storage in isolation, would access a sibling invocation's
> already-computed (or not-yet-computed, depending on scheduling) input storage.
> Needs its own real IR reduction of one of these exact `cache.*` cases to
> confirm the precise cross-indexing shape before designing a fix
