---
model: claude-sonnet-5
resume: 3e3ed1ca-e8e0-43ee-a165-5cdf3bba2524
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
letter deep going forward (i.e. Q54(a)).

The offload-test-suite checked out at /home/dev/dev/offload-test-suite has a
branch named feme on the remote at
https://github.com/llvm-beanz/offload-test-suite.git, which adds a generated set
of targets to run the tests against the feme ICD (check-hlsl-feme-vk).

Break your changes into small code changes with each change committed
spearately. Record your thought process into a file named "agent_thoughts.md" at
the root of the repository, appending to the file under a new top-level heading
if it already exists, and commit it in its own commit when you're done. Please
consult the i-have-adhd skill (from ~/.agents/skills) when writing the
agent_thoughts.md file. Please include suggested next steps if applicable in the
agent thoughts.

# Request

Can you work on H99a or other blocking work to make progress on the H-series
milestones?

> **`pipeline.fast_linked_library.blend.dual_source`'s two-part 3469-case
> failure family** (newly exposed by H99's own closing full-family re-run, once
> the hang and crash that previously masked most of the family were fixed): (1)
> 2878 cases fail with `Fail (Image mismatch)` -- spread broadly across
> `format.*`/`multi_attachments.*` subfamilies and blend-state combinations, not
> yet reduced to a specific shape; (2) 591 cases fail with
> `VK_ERROR_INITIALIZATION_FAILED` at `vkQueueSubmit`, concentrated entirely in
> exactly three formats (`r16_sfloat`, `r16g16_sfloat`, `r32g32b32_sfloat`, ~197
> cases each) across both `format.*` and `multi_attachments.*` -- all three are
> non-power-of-two-friendly or single/dual-channel float formats a real GPU
> would commonly report as unable to support color-attachment blending,
> suggesting the driver may be missing a
> `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` capability check for these
> specific formats and should be returning `NotSupported` rather than failing at
> submit time. Not yet triaged -- needs (1) a qpa-image/channel-level pixel
> reduction of a representative image-mismatch case (mirroring H88/H93's own
> technique) to determine which part of the dual-source-blend pipeline
> disagrees, and (2) tracing why `vkQueueSubmit` fails specifically for
> `r16_sfloat`/`r16g16_sfloat`/`r32g32b32_sfloat` to decide whether the fix is a
> missing format-capability check (report `NotSupported` up front) or a genuine
> renderer bug for these formats
