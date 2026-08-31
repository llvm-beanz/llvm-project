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

Can you continue working on H7k or other prerequisites of the H-series
milestones from the roadmap?

> **Point/line-primitive quad coverage excludes exact pixel-grid-aligned
> centers.** Found via H7d's own real
> `deqp-vk.clipping.clip_volume.depth_clamp.{line_list,line_strip,point_list}`
> reproduction: every one of that CTS case's own point/line vertices lands with
> its rasterized quad centered exactly on an integer pixel-grid intersection
> (e.g. a point at `(8,8)` expands to a `[7.5,8.5]x[7.5,8.5]` quad), and
> `Executor.cpp`'s `pushQuadTriangle`/coverage test excludes the covering pixel
> on *both* sides of that boundary, rendering zero pixels for a primitive that
> should render some. Nothing in this project's own unit-test coverage exercises
> a point-topology draw at all yet, so this is a pre-existing gap, not a
> regression from H7d. Needs the coverage test's own inclusive/exclusive edge
> convention audited against a real GPU's fill rule (the classic "top-left"
> rule, or equivalent) so an exact grid-aligned primitive is guaranteed exactly
> one consistent side of coverage, not both-excluded
