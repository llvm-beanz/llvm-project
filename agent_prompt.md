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

Can you work on H9c or other prerequisites blocking the H-series milestones?

> **`vkCreateGraphicsPipelines` fails a tessellation-control-shader pipeline
> with `"feme-cpu-wrap-patch-constant: masked output store references an unknown
> patch-output signature element"`**, discovered by H9a's own CTS re-run once
> more pipelines began clearing pipeline creation and reaching this
> stage-wrapping code for the first time. Hit by 1,276 real
> `dEQP-VK.query_pool.statistics_query.clipping_invocations.*_tessellation*`-shaped
> cases (and likely every other CTS group exercising a tessellation-control
> shader's patch-constant function against this ICD). Entirely unrelated to
> H9a's own fragment-stage/color-attachment scope -- a distinct gap in
> `feme-cpu-wrap-patch-constant`'s own handling of a masked (partially-written,
> e.g. only some patch-output locations actually stored to by the patch-constant
> function) output store, needing its own real IR reduction of one of these
> exact cases (the same technique this project's H6-series/H8-series/H9-series
> chains have used throughout) to isolate whether the patch-output signature
> itself is being built incorrectly, or the masked-store lowering simply doesn't
> know how to look up an element that a real signature does contain
