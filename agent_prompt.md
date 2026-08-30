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

Can you continue working on H7t?

> **A fragment stage's color output with fewer than 4 components (e.g. a `vec3`)
> at a *used* color-attachment location is rejected outright**, discovered via
> H7s's own re-run of `alpha_to_coverage_unused_attachment.*` (its own real
> attachment's fragment output, `fragColor1 = vtxColor.rgb`, is a `vec3` at
> location 1 -- legal per spec, with the missing components implicitly defined,
> alpha defaulting to `1.0`). `GraphicsPipeline.cpp`'s `validateStageInterfaces`
> (`Color->ComponentCount != 4`) and `Executor.cpp`'s fragment-output linkage
> (`FSColor->ComponentCount != 4` at line 1377, and the equivalent checks for a
> second color output and the alpha-to-coverage element) all hard-require
> exactly 4 components for a real (used) attachment's own output; this is
> unrelated to H7s's own unused-attachment-slot mechanism (confirmed: H7s's own
> unit tests, all full 4-component outputs, pass cleanly with no regression).
> Needs a real survey of what padding/defaulting a fragment output narrower than
> 4 components requires end to end (accepting `ComponentCount` in `{1, 2, 3, 4}`
> at `validateStageInterfaces`, then `Executor.cpp` synthesizing the missing
> trailing components -- `0.0` for a missing G/B, `1.0` for a missing A,
> mirroring how a narrower vertex *input* attribute is already zero/one-extended
> elsewhere in this codebase)
