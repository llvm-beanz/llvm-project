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

Please investigate and fix the issues tracked by milestone L2:

> **A `gpu-exec: error: Failed to create compute pipeline. (VkResult = -3)`
> (`VK_ERROR_OUT_OF_HOST_MEMORY`'s numeric value, but almost certainly masking a
> real, more specific internal failure this ICD reports generically) bucket** --
> re-counted with a fresh, correctly-`VK_ICD_FILENAMES`-absolute-pathed
> `check-hlsl-feme-vk` re-run after L1/L4 landed: 175 distinct failing cases hit
> this message (184 before L1/L4, i.e. L1/L4's own fixes only removed 9 of this
> bucket's cases in passing; this row's own original "47" estimate was measured
> with a different, since-lost counting method and should be treated as stale --
> 175/184 is this row's own first reliably-reproducible count). Still not
> triaged past the raw `VkResult`; needs the same real-ICD-plus-diagnostic
> technique every H-row above used (temporarily instrumenting the
> compute-pipeline-creation path with a debug print of the real internal error
> before it collapses to this generic code) to find whether this is one dominant
> root cause or several -- by far the largest single remaining bucket, so
> highest-priority to scope next
