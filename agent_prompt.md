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

Can you work on H5 or other prerequisites blocking the H-series milestones?

> **Geometry stage.** Same rejection as H4, same milestone (G5), but an
> independent feature bit (`geometryShader`), an independent limit block
> (`maxGeometry*`), stream output, and `multiviewGeometryShader` now that H2 has
> landed. Whole `dEQP-VK.geometry` group (partially done, and broken down below
> the same way H4 was: H5a closes the execution-mode half of "reflect a geometry
> entry point at all" (see H5a's own row for what remains of that -- the
> per-vertex input addressing gap it found is H5b, a genuinely new piece of
> machinery neither H4's tessellation precedent nor G5's own wrapper work
> needed, since a hull control-point phase's own per-control-point inputs are
> restricted to "this invocation's own", never an arbitrary dynamically-indexed
> one the way a geometry entry's `gl_in[i]` is). `dEQP-VK.geometry.*` is
> unaffected so far, still 0 `Pass`/0 `Fail`/200 `NotSupported` -- correctly so,
> since `CanonicalizeStagePass::run` deliberately does not yet accept
> `ShaderStage::Geometry` (H5a's own report entry explains why not doing so was
> the right call, not an oversight). This row stays open until H5a-H5e all
> close)
