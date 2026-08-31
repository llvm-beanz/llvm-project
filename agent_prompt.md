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

Can you continue working on H19k or any prerequisite work required to complete
the H-series milestones?

> **`feme-cpu-linearize`'s own inability to linearize a loop containing an
> internal branch in `Flow`**, discovered as a hard, unrelated prerequisite
> blocking H19g's own real CTS closure: every
> `dEQP-VK.image.load_store_multisample.2d.*` verification shader contains a
> `for (int sampleNdx = 0; sampleNdx < N; ++sampleNdx) {
> imageStore/imageLoad(...) }` loop that `feme-cpu-linearize` rejects at
> pipeline-creation time with "loop ... has an internal branch in 'Flow';
> unsupported", regardless of how complete the storage-image addressing side is
> -- confirmed via a real CTS re-run with `shaderStorageImageMultisample`
> temporarily forced `VK_TRUE`: 0/84 real passes, 27/84 hit this exact error,
> the remaining 57/84 `NotSupported` on formats outside today's mandatory floor.
> Needs a real investigation into `feme-cpu-linearize`'s own
> control-flow-linearization algorithm (`feme/lib/Transforms/CPU/` -- exact file
> not yet identified) to determine why this particular loop shape's own internal
> branch is unsupported (a simple bounded counting loop with a
> compile-time-constant trip count, structurally unlike the more complex
> divergent-control-flow cases this milestone's own `Flow`-based linearization
> already handles elsewhere) and what a fix looks like -- likely its own, larger
> milestone given the "roadmap milestone 6 deviation" note already attached to
> the existing error message, not a narrow follow-on
