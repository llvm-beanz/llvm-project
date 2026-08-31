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

Can you continue working on H19l or any prerequisite work required to complete
the H-series milestones?

> **`feme-cpu-simdize`'s own inability to decompose a divergent vector value
> used outside its supported use-pattern set**, discovered as a new, distinct
> prerequisite immediately downstream of H19k's own closure: with H19k's
> Flow-fold fix in place and `shaderStorageImageMultisample` temporarily forced
> `VK_TRUE` to probe the real shader path, all 27 of
> `dEQP-VK.image.load_store_multisample.2d.*`'s
> previously-`feme-cpu-linearize`-blocked cases now instead fail pipeline
> creation with `feme-cpu-simdize: function 'main' has a divergent vector value
> '' used outside a supported
> insertelement-chain/resource-store/extractelement/select/shufflevector/phi/elementwise/comparison/reduce/vectorizable-intrinsic
> pattern; component decomposition is not yet supported for this use (roadmap
> milestone 7 deviation)` -- a pre-existing `feme-cpu-simdize` scope limit (per
> its own "roadmap milestone 7 deviation" note), not anything H19k's own
> linearizer fold introduced. Needs a real IR reduction of this exact case (the
> same technique the H6g-b/H6j/H6k/H6l/H19k chain has used throughout) to
> identify the specific divergent-vector use shape this loop's own per-sample
> body produces that falls outside `feme-cpu-simdize`'s current
> supported-pattern set, and what a fix looks like -- likely its own, larger
> milestone given the existing "roadmap milestone 7 deviation" note, not a
> narrow follow-on
